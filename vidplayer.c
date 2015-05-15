/*
Prototype video "player" with libav.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/pthread.h>
#include <toaru/spinlock.h>
#include <toaru/list.h>

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;

#define BUFFER_LEN 128
static volatile void * volatile buffer[BUFFER_LEN] = {0};
static volatile size_t read_ptr = 0;
static volatile size_t write_ptr = 0;

static void * buffer_read(void) {
	do {
		if (read_ptr != write_ptr) {
			volatile void * out = buffer[read_ptr];
			buffer[read_ptr] = 0;
			read_ptr = (read_ptr + 1) % BUFFER_LEN;
			return (void *)out;
		}
		syscall_yield();
	} while (1);
}

static void buffer_write(void * target) {
	do {
		if ((write_ptr >= read_ptr ||
		    write_ptr < read_ptr - 1) &&
		    !((write_ptr == BUFFER_LEN-1) && (read_ptr == 0))) {
			buffer[write_ptr] = target;
			write_ptr = (write_ptr + 1) % BUFFER_LEN;
			return;
		}
		syscall_yield();
	} while (1);
}

typedef struct {
	size_t number;
	size_t pts;
	int width;
	int height;
	char data[];
} my_frame;


static double tmp;


static void * playback_thread(void * garbage) {
	struct timeval tv;
	int64_t start_time = 0;
	gettimeofday(&tv, NULL);
	start_time = tv.tv_sec * 1000000 + tv.tv_usec;

	while (1) {
		my_frame * frame = buffer_read();

		if (!frame) {
			fprintf(stderr, "Something is wrong, frame was zero. Bail.\n");
			break;
		}

		if (frame->width == 0 && frame->height == 0) break;

		printf("\rFrame [%lld]", frame->number);
		printf(" pts: %lld    ", frame->pts);
		fflush(stdout);

		int64_t new_time = 0;
		gettimeofday(&tv, NULL);
		new_time = tv.tv_sec * 1000000 + tv.tv_usec;

		while (new_time - start_time < frame->pts * tmp) {
			if (frame->pts * tmp - (new_time - start_time) > 2000) {
				syscall_yield();
			}
			gettimeofday(&tv, NULL);
			new_time = tv.tv_sec * 1000000 + tv.tv_usec;
		}

		memcpy(ctx->backbuffer, &frame->data, frame->width * frame->height * 4);
		free(frame);

		flip(ctx);
		yutani_flip(yctx, wina);
		syscall_yield();
	}

	pthread_exit(NULL);
}

static my_frame death_packet = {
	0, 0, 0
};

static int decode_frames(int s, AVFormatContext * format_ctx, AVCodecContext * codec_ctx, AVCodec * codec) {

	AVPacket packet;
	AVFrame * frame;
	int framedone;

	struct SwsContext * swctx;

	frame = av_frame_alloc();

	if (!frame) {
		fprintf(stderr, "frak, out of memz\n");
	}

	int w = codec_ctx->width;
	int h = codec_ctx->height;

	yctx = yutani_init();
	wina = yutani_window_create(yctx, w, h);
	yutani_window_move(yctx, wina, 300, 300);
	ctx = init_graphics_yutani_double_buffer(wina);

	draw_fill(ctx, rgb(127, 0, 127));
	flip(ctx);
	yutani_flip(yctx, wina);

	/* Quick, init swscale */
	fprintf(stderr, "Width = %d, Height = %d, converting from format #%d...\n", frame->width, frame->height, frame->format);

	swctx = sws_getContext(w, h, codec_ctx->pix_fmt, w, h, AV_PIX_FMT_RGB32, 0, 0, 0, 0);

	uint8_t *dst_data[4];
	int dst_linesize[4];
	av_image_alloc(dst_data, dst_linesize, w, h, AV_PIX_FMT_RGB32, 1);

	tmp = (double)format_ctx->streams[s]->time_base.num / (double)format_ctx->streams[s]->time_base.den * 1000000;

	int i = 0;

	while (av_read_frame(format_ctx, &packet) >= 0) {
		if (packet.stream_index == s) {
			avcodec_decode_video2(codec_ctx, frame, &framedone, &packet);

			if (framedone) {
				i++;

				sws_scale(swctx, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);

				my_frame * f = malloc(sizeof(my_frame) + w * h * 4);
				f->number = frame->coded_picture_number;
				f->pts = frame->pkt_pts;
				f->width = w;
				f->height = h;
				memcpy(&f->data, dst_data[0], w * h * 4);
				buffer_write(f);

			}
		}
		av_free_packet(&packet);
	}


	buffer_write(&death_packet);

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

	/* start playback thread */
	pthread_t playback;
	pthread_create(&playback, NULL, playback_thread, NULL);

	/* Frames! */
	decode_frames(video_stream_index, format_ctx, codec_ctx, codec);

	avcodec_close(codec_ctx);
	avformat_close_input(&format_ctx);

	waitpid(playback.id, NULL, 0);


	return 0;
}
