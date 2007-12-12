// VideoDecoderFfmpeg.h: Video decoding using the FFMPEG library.
// 
//   Copyright (C) 2007 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

// $Id: VideoDecoderFfmpeg.h,v 1.10 2007/12/12 10:23:07 zoulunkai Exp $

#ifndef __VIDEODECODERFFMPEG_H__
#define __VIDEODECODERFFMPEG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "log.h"
#include "VideoDecoder.h"

extern "C" {
#include <ffmpeg/avcodec.h>
}


namespace gnash {
namespace media {


class VideoDecoderFfmpeg : public VideoDecoder {
	
public:
	VideoDecoderFfmpeg();
	~VideoDecoderFfmpeg();

	virtual unsigned getPaddingBytes() const { return FF_INPUT_BUFFER_PADDING_SIZE; }

	bool setup(VideoInfo* info);

	bool setup(
		int /*width*/,
		int /*height*/,
		int /*deblocking*/,
		bool /*smoothing*/,
		videoCodecType /*format*/,
		int /*outputFormat*/);

	boost::uint8_t* decode(boost::uint8_t* input, boost::uint32_t inputSize, boost::uint32_t& outputSize);

	std::auto_ptr<image::image_base> decodeToImage(boost::uint8_t* /*input*/, boost::uint32_t /*inputSize*/);

	static boost::uint8_t* convertRGB24(AVCodecContext* srcCtx, AVFrame* srcFrame);

private:

	AVCodec* _videoCodec;
	AVCodecContext* _videoCodecCtx;

};
	
} // gnash.media namespace 
} // gnash namespace

#endif // __VIDEODECODERFFMPEG_H__
