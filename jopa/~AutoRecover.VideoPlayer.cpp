// ffmpeg_tester.cpp : Defines the entry point for the console application.
//

#include "VideoPlayer.h"
#include <stdio.h>

#define _SDL(expr)\
	{\
	SDL_ClearError(); \
if (expr){\
fprintf(stderr, #expr " error %s\n", SDL_GetError()); \
return -1; \
}\
	}

#define _SDLP(expr)\
{\
	SDL_ClearError(); \
	if (NULL == (expr)){\
		fprintf(stderr, #expr " error %s\n", SDL_GetError()); \
		return -1; \
	}\
}

#define _AVE(expr)\
{\
	int err = expr; \
if (err < 0){\
fprintf(stderr, #expr " error %d\n", err); \
return -1; \
}\
}

#include <SDL.h>
#include <SDL_thread.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

template <typename T>
class ffmpeg_auto_ptr
{
public:

	ffmpeg_auto_ptr(T *pT = 0)
	{
		assign(pT);
	}
	~ffmpeg_auto_ptr()
	{
		assign(0);
	}

	T *operator->()
	{
		return m_pT;
	}

	const T *operator->() const
	{
		return m_pT;
	}

	ffmpeg_auto_ptr &operator=(T *pT)
	{
		assign(pT);
		return *this;
	}

	T **operator &()
	{
		assign(0);
		return &m_pT;
	}

	operator T *() 
	{
		return m_pT;
	}
private:
	T *m_pT;

	void operator=(const ffmpeg_auto_ptr<T>&);

	void assign(T *pT)
	{
		if (m_pT)
		{
			destroy();
		}
		m_pT = pT;
	}

	void destroy(){}
};

template <>
void ffmpeg_auto_ptr<AVFrame>::destroy()
{
	av_frame_free(&m_pT);
}

template <>
void ffmpeg_auto_ptr<AVCodecContext>::destroy()
{
	avcodec_close(m_pT);
}

template <>
void ffmpeg_auto_ptr<AVFormatContext>::destroy()
{
	avformat_close_input(&m_pT);
}

template <>
void ffmpeg_auto_ptr<SwsContext>::destroy()
{
	sws_freeContext(m_pT);
}
template <>
void ffmpeg_auto_ptr<SDL_Texture>::destroy()
{
	SDL_DestroyTexture(m_pT);
}
template <>
void ffmpeg_auto_ptr<SDL_Renderer>::destroy()
{
	SDL_DestroyRenderer(m_pT);
}

template <>
void ffmpeg_auto_ptr<SDL_Window>::destroy()
{
	SDL_DestroyWindow(m_pT);
}

template <>
void ffmpeg_auto_ptr<AVPacket>::destroy()
{
	av_free_packet(m_pT);
}


template <>
void ffmpeg_auto_ptr<uint8_t>::destroy()
{
	av_free(m_pT);
}


int PlayVideoFile(const char *szUrl)
{
	_SDL(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER));

	av_register_all();

	ffmpeg_auto_ptr<AVFormatContext> pFormatCtx;

	// Open video file
	_AVE(avformat_open_input(&pFormatCtx, szUrl, NULL, NULL));

	// Retrieve stream information
	_AVE(avformat_find_stream_info(pFormatCtx, NULL));

	// Dump information about file onto standard error
	//dump_format(pFormatCtx, 0, argv[1], 0);

	unsigned int i;
	ffmpeg_auto_ptr<AVCodecContext> pCodecCtx;

	// Find the first video stream
	int videoStream = -1;
	for (i = 0; i<pFormatCtx->nb_streams; i++)
	if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
		videoStream = i;
		break;
	}
	if (videoStream == -1)
		return -1; // Didn't find a video stream

	// Get a pointer to the codec context for the video stream
	pCodecCtx = pFormatCtx->streams[videoStream]->codec;

	int screenWidth = 352, screenHeight = 288;

	ffmpeg_auto_ptr<SDL_Window> screen;
	_SDLP(screen = SDL_CreateWindow("FFMPEG Player",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL));

	ffmpeg_auto_ptr<SDL_Renderer> render;
	_SDLP(render = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));


	SDL_DisplayMode mode;
	_SDL(SDL_GetCurrentDisplayMode(0, &mode));

	ffmpeg_auto_ptr<SDL_Texture> texture;
	_SDLP(texture = SDL_CreateTexture(render,
		mode.format,
		SDL_TEXTUREACCESS_STREAMING,
		screenWidth, screenHeight));

	AVPixelFormat textureFormat;
	switch (mode.format)
	{
	case SDL_PIXELFORMAT_RGB888:
		textureFormat = AV_PIX_FMT_BGRA;
		break;
	case SDL_PIXELFORMAT_RGBX8888:
		textureFormat = AV_PIX_FMT_ARGB;
		break;
	default:
		fprintf(stderr, "unsupported video pixel format %x\n", pCodecCtx->pix_fmt);
		return -1;
		break;
	}
	ffmpeg_auto_ptr<SwsContext> swsContext =  sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		screenWidth, screenHeight, textureFormat, SWS_BICUBIC, NULL, NULL, NULL);

	ffmpeg_auto_ptr<AVCodec> pCodec;

	// Find the decoder for the video stream
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}
	// Open codec
	_AVE(avcodec_open2(pCodecCtx, pCodec, NULL));

	ffmpeg_auto_ptr<AVFrame> outputPic (av_frame_alloc());

	// Allocate a buffer large enough for all data
	int size;
	_AVE(size = avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height));
	ffmpeg_auto_ptr<uint8_t> buffer( (uint8_t*)av_malloc(size));
	if (buffer == NULL) {
		fprintf(stderr, "av_malloc error!\n");
		return -1; // Codec not found
	}

	// Initialize frame->linesize and frame->data pointers
	_AVE(avpicture_fill((AVPicture*)outputPic.operator AVFrame *(), buffer, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height));

	int counter = 0;

	while (true)
	{


		AVPacket packet;
		while (true) {

			ffmpeg_auto_ptr<AVPacket> lifetime(&packet);

			int err = av_read_frame(pFormatCtx, &packet);

			if (err < 0)
			{
				if (AVERROR_EOF == err)
				{
					printf("reached EOF\n");
					break;
				}
				else
				{
					int aviErr = -err;
					printf("error %d(%.4s)\n", err, &aviErr);
				}
			}


			// Is this a packet from the video stream?
			if (packet.stream_index == videoStream) {

				// Decode video frame
				int frameFinished(0);
				_AVE(avcodec_decode_video2(pCodecCtx, outputPic, &frameFinished, &packet));

				// Did we get a video frame?
				if (frameFinished)
				{
					counter++;

					AVPicture texturePic;
					uint8_t *pixels; int pitch;
					_SDL(SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch));

					_AVE(avpicture_fill(&texturePic, pixels, textureFormat, pitch / 4, screenHeight));

					_AVE(sws_scale(swsContext, outputPic->data, outputPic->linesize,
						0, pCodecCtx->height, texturePic.data, texturePic.linesize));

					SDL_UnlockTexture(texture);

					_SDL(SDL_RenderClear(render)); // Fill render with color
					_SDL(SDL_RenderCopy(render, texture, NULL, NULL)); // Copy the texture into render
					SDL_RenderPresent(render); // Show render on window


					int left = outputPic->pkt_duration * pCodecCtx->framerate.den / pCodecCtx->framerate.num;

					while (left > 0)
					{
						clock_t now = clock();
						SDL_Event       event;
						if (SDL_WaitEventTimeout(&event, left))
						{
							SDL_PumpEvents();
						}

						if (event.type == SDL_QUIT)
						{
							printf("got quit event\n");
							return 0;
						}

						left -= (clock() - now) * 1000 / CLOCKS_PER_SEC;
					}
				}
			}
		}
	
		_AVE(av_seek_frame(pFormatCtx, 0, 0, AVSEEK_FLAG_FRAME));
	}


	return 0;
}
