#include <stdio.h>
#include "stdafx.h"

#include <SDL.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}


int main(int argc, char* argv[]) {
	if (argc < 2) {
		printf("Usage: %s filename\n", argv[0]);
		return 0;
	}

	// Register all available file formats and codecs
	av_register_all();

	int err;
	// Init SDL with video support
	err = SDL_Init(SDL_INIT_VIDEO);
	if (err < 0) {
		fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
		return -1;
	}

	// Open video file
	const char* filename = argv[1];
	AVFormatContext* format_context = NULL;
	// Read file header and store info in AVFormatContext struct
	err = avformat_open_input(&format_context, filename, NULL, NULL);
	if (err < 0) {
		fprintf(stderr, "ffmpeg: Unable to open input file\n");
		return -1;
	}

	// Retrieve stream information
	// Now we have existing streams in format_context->streams and number of streams format_context->nb_streams
	err = avformat_find_stream_info(format_context, NULL);
	if (err < 0) {
		fprintf(stderr, "ffmpeg: Unable to find stream info\n");
		return -1;
	}

	// Print information about file and all streams onto standard error
	av_dump_format(format_context, 0, argv[1], 0);

	// Find the first video stream
	int video_stream;
	for (video_stream = 0; video_stream < format_context->nb_streams; ++video_stream) {
		if (format_context->streams[video_stream]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			break;
		}
	}
	if (video_stream == format_context->nb_streams) {
		fprintf(stderr, "ffmpeg: Unable to find video stream\n");
		return -1;
	}

	// AVCodecContext - information about the codec
	AVCodecContext* codec_context = format_context->streams[video_stream]->codec;
	// AVCodec - codec itself
	AVCodec* codec = avcodec_find_decoder(codec_context->codec_id);
	// Open codec
	err = avcodec_open2(codec_context, codec, NULL);
	if (err < 0) {
		fprintf(stderr, "ffmpeg: Unable to open codec\n");
		return -1;
	}

	// Set width and height
	SDL_Surface* screen = SDL_SetVideoMode(codec_context->width, codec_context->height, 0, 0);
	if (screen == NULL) {
		fprintf(stderr, "Couldn't set video mode\n");
		return -1;
	}

	// Create overlay
	SDL_Overlay* bmp = SDL_CreateYUVOverlay(codec_context->width, codec_context->height,
		SDL_YV12_OVERLAY, screen);

	struct SwsContext* img_convert_context;
	// Create convert context
	img_convert_context = sws_getCachedContext(NULL,
		codec_context->width, codec_context->height,
		codec_context->pix_fmt,
		codec_context->width, codec_context->height,
		AV_PIX_FMT_YUV420P, SWS_BICUBIC,
		NULL, NULL, NULL);
	if (img_convert_context == NULL) {
		fprintf(stderr, "Cannot initialize the conversion context\n");
		return -1;
	}

	// Allocate AVFrame - decoded frame structure
	AVFrame* frame = av_frame_alloc();
	// Create AVPacket - encoded frame structure
	AVPacket packet;
	// Read frame by frame and store it in AVPacket. If returns >= 0 - no error
	while (av_read_frame(format_context, &packet) >= 0) {
		if (packet.stream_index == video_stream) {
			// Video stream packet
			int frame_finished;
			// Decode AVPacket
			avcodec_decode_video2(codec_context, frame, &frame_finished, &packet);

			// frame_finished is positive if AVFrame completely decoded. AVFrame can be stored in multiple AVPackets
			if (frame_finished) {
				SDL_LockYUVOverlay(bmp);

				// Convert frame to YV12 pixel format for display in SDL overlay

				AVPicture pict;
				pict.data[0] = bmp->pixels[0];
				pict.data[1] = bmp->pixels[2];  // it's because YV12
				pict.data[2] = bmp->pixels[1];

				pict.linesize[0] = bmp->pitches[0];
				pict.linesize[1] = bmp->pitches[2];
				pict.linesize[2] = bmp->pitches[1];

				sws_scale(img_convert_context,
					frame->data, frame->linesize,
					0, codec_context->height,
					pict.data, pict.linesize);

				SDL_UnlockYUVOverlay(bmp);

				SDL_Rect rect;
				rect.x = 0;
				rect.y = 0;
				rect.w = codec_context->width;
				rect.h = codec_context->height;
				SDL_DisplayYUVOverlay(bmp, &rect);
			}
		}

		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);

		// Handling SDL events in order OS not to deal with us like we are frozen
		SDL_Event event;
		if (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				break;
			}
		}
	}

	sws_freeContext(img_convert_context);

	// Free the YUV frame
	av_free(frame);

	// Close the codec
	avcodec_close(codec_context);

	// Close the video file
	avformat_close_input(&format_context);

	// Quit SDL
	SDL_Quit();
	return 0;
}
