// SPDX-License-Identifier: GPL-3.0-or-later
// Ogg Theora movies, the portable stand-in for Bink cinematics (SoundTheora.h).

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "SoundBink.h"
#include "SoundTheora.h"

#include <theora/theoradec.h>

namespace {

// how much of the file is read at a time, and how much may go by before the three
// Theora headers have all been found
const int		READ_CHUNK			= 64 * 1024;
const int		MAX_HEADER_BYTES	= 4 * 1024 * 1024;
// how many frames one ImageForTime may decode to catch up after a stall
const int		MAX_CATCH_UP		= 30;

}

struct theoraState_t {
	ogg_sync_state		sync;
	ogg_stream_state	stream;
	th_info				info;
	th_comment			comment;
	th_setup_info*		setup;
	th_dec_ctx*			decoder;
	bool				haveStream;
	bool				endOfFile;
	int					bytesRead;
};

sdSoundTheoraMovie::sdSoundTheoraMovie() :
	state( NULL ),
	file( NULL ),
	videoBuffer( NULL ),
	width( 0 ),
	height( 0 ),
	fpsNumerator( 0 ),
	fpsDenominator( 1 ),
	framesDecoded( 0 ),
	startTime( 0 ),
	playing( false ) {
}

sdSoundTheoraMovie::~sdSoundTheoraMovie() {
	Close();
}

void sdSoundTheoraMovie::Close() {
	if ( state != NULL ) {
		if ( state->decoder != NULL ) {
			th_decode_free( state->decoder );
		}
		if ( state->setup != NULL ) {
			th_setup_free( state->setup );
		}
		if ( state->haveStream ) {
			ogg_stream_clear( &state->stream );
		}
		th_comment_clear( &state->comment );
		th_info_clear( &state->info );
		ogg_sync_clear( &state->sync );
		delete state;
		state = NULL;
	}
	if ( file != NULL ) {
		fileSystem->CloseFile( file );
		file = NULL;
	}
	if ( videoBuffer != NULL ) {
		Mem_Free( videoBuffer );
		videoBuffer = NULL;
	}
	width = height = 0;
	fpsNumerator = 0;
	fpsDenominator = 1;
	framesDecoded = 0;
	playing = false;
}

// feeds the next piece of the file to the Ogg sync layer; false at the end
bool sdSoundTheoraMovie::ReadMore() {
	if ( state->endOfFile ) {
		return false;
	}
	char* buffer = ogg_sync_buffer( &state->sync, READ_CHUNK );
	if ( buffer == NULL ) {
		state->endOfFile = true;
		return false;
	}
	const int read = file->Read( buffer, READ_CHUNK );
	if ( read <= 0 ) {
		state->endOfFile = true;
		return false;
	}
	ogg_sync_wrote( &state->sync, read );
	state->bytesRead += read;
	return true;
}

/*
The first page of each logical stream comes first in the file (RFC 3533); the Theora
one is the stream whose first packet th_decode_headerin takes. Its three header
packets follow, and the first packet it does not take as a header is the first frame,
left queued in the stream.
*/
bool sdSoundTheoraMovie::ReadHeaders() {
	int headers = 0;
	ogg_page page;
	ogg_packet packet;
	while ( headers < 3 ) {
		if ( state->bytesRead > MAX_HEADER_BYTES ) {
			return false;
		}
		// packets already queued in the Theora stream
		if ( state->haveStream ) {
			bool needPage = false;
			while ( headers < 3 ) {
				const int peek = ogg_stream_packetpeek( &state->stream, &packet );
				if ( peek == 0 ) {
					needPage = true;
					break;
				}
				if ( peek < 0 ) {
					ogg_stream_packetout( &state->stream, &packet );	// a hole: skip it
					continue;
				}
				const int result = th_decode_headerin( &state->info, &state->comment, &state->setup, &packet );
				if ( result < 0 ) {
					return false;
				}
				if ( result == 0 ) {
					return false;	// a frame before the last header
				}
				ogg_stream_packetout( &state->stream, &packet );
				headers++;
			}
			if ( !needPage ) {
				break;
			}
		}
		// the next page: a new stream's first page, or more of the Theora stream
		const int pageResult = ogg_sync_pageout( &state->sync, &page );
		if ( pageResult == 0 ) {
			if ( !ReadMore() ) {
				return false;
			}
			continue;
		}
		if ( pageResult < 0 ) {
			continue;	// skipped bytes to the next page
		}
		if ( state->haveStream ) {
			ogg_stream_pagein( &state->stream, &page );	// another stream's page is refused
			continue;
		}
		if ( !ogg_page_bos( &page ) ) {
			return false;	// every first page went by without a Theora stream
		}
		ogg_stream_state test;
		if ( ogg_stream_init( &test, ogg_page_serialno( &page ) ) != 0 ) {
			return false;
		}
		ogg_stream_pagein( &test, &page );
		if ( ogg_stream_packetpeek( &test, &packet ) == 1 &&
			th_decode_headerin( &state->info, &state->comment, &state->setup, &packet ) > 0 ) {
			ogg_stream_packetout( &test, &packet );
			state->stream = test;
			state->haveStream = true;
			headers = 1;
		} else {
			ogg_stream_clear( &test );
		}
	}
	return headers == 3;
}

bool sdSoundTheoraMovie::Open( const char* moviePath ) {
	Close();
	if ( moviePath == NULL || moviePath[ 0 ] == '\0' ) {
		return false;
	}
	file = fileSystem->OpenFileRead( moviePath );
	if ( file == NULL ) {
		return false;
	}

	state = new theoraState_t;
	memset( state, 0, sizeof( *state ) );
	ogg_sync_init( &state->sync );
	th_info_init( &state->info );
	th_comment_init( &state->comment );

	if ( !ReadHeaders() ) {
		common->Warning( "'%s' is not an Ogg Theora movie", moviePath );
		Close();
		return false;
	}

	const th_info& info = state->info;
	if ( info.pixel_fmt != TH_PF_420 ) {
		common->Warning( "'%s': only 4:2:0 Theora movies play (encode with -pix_fmt yuv420p)", moviePath );
		Close();
		return false;
	}
	// the picture, on even lines and columns so the 4:2:0 planes stay aligned
	width = static_cast< int >( info.pic_width ) & ~1;
	height = static_cast< int >( info.pic_height ) & ~1;
	if ( width <= 0 || height <= 0 || width > MAX_DIMENSION || height > MAX_DIMENSION ||
		info.pic_x + info.pic_width > info.frame_width || info.pic_y + info.pic_height > info.frame_height ) {
		common->Warning( "'%s' has an unsupported picture size", moviePath );
		Close();
		return false;
	}
	if ( info.fps_numerator == 0 || info.fps_denominator == 0 ||
		info.fps_numerator > 1000000u || info.fps_denominator > 1000000u ) {
		common->Warning( "'%s' has no frame rate", moviePath );
		Close();
		return false;
	}
	fpsNumerator = static_cast< int >( info.fps_numerator );
	fpsDenominator = static_cast< int >( info.fps_denominator );

	state->decoder = th_decode_alloc( &state->info, state->setup );
	th_setup_free( state->setup );
	state->setup = NULL;
	if ( state->decoder == NULL ) {
		Close();
		return false;
	}

	const int lumaSize = width * height;
	const int chromaSize = ( width >> 1 ) * ( height >> 1 );
	videoBuffer = static_cast< byte* >( Mem_Alloc( lumaSize + chromaSize * 2 ) );
	memset( videoBuffer, 0, lumaSize );
	memset( videoBuffer + lumaSize, 128, chromaSize * 2 );

	playing = true;
	if ( !DecodeNextFrame( true ) ) {
		Close();
		return false;
	}
	startTime = Sys_Milliseconds();
	common->Printf( "Theora cinematic opened: %s (%dx%d, %.2f fps)\n", moviePath, width, height,
		static_cast< float >( fpsNumerator ) / fpsDenominator );
	return true;
}

// decodes the next frame, copying it out when asked; false at the end of the movie
bool sdSoundTheoraMovie::DecodeNextFrame( bool copyOut ) {
	ogg_packet packet;
	ogg_page page;
	for ( ;; ) {
		const int result = ogg_stream_packetout( &state->stream, &packet );
		if ( result > 0 ) {
			ogg_int64_t granule = 0;
			const int decoded = th_decode_packetin( state->decoder, &packet, &granule );
			if ( decoded < 0 && decoded != TH_DUPFRAME ) {
				continue;	// a bad packet: the next one
			}
			framesDecoded++;
			if ( copyOut ) {
				CopyFrame();
			}
			return true;
		}
		if ( result < 0 ) {
			continue;	// a hole in the stream
		}
		// the stream needs another page
		const int pageResult = ogg_sync_pageout( &state->sync, &page );
		if ( pageResult > 0 ) {
			ogg_stream_pagein( &state->stream, &page );
		} else if ( pageResult == 0 && !ReadMore() ) {
			return false;
		}
	}
}

// the decoder's picture into videoBuffer, as Y, U, V planes
void sdSoundTheoraMovie::CopyFrame() {
	th_ycbcr_buffer planes;
	if ( th_decode_ycbcr_out( state->decoder, planes ) != 0 ) {
		return;
	}
	const th_info& info = state->info;
	const int picX = static_cast< int >( info.pic_x ) & ~1;
	const int picY = static_cast< int >( info.pic_y ) & ~1;
	byte* out = videoBuffer;
	for ( int plane = 0; plane < 3; plane++ ) {
		const int shift = plane == 0 ? 0 : 1;
		const int planeWidth = width >> shift;
		const int planeHeight = height >> shift;
		const th_img_plane& source = planes[ plane ];
		if ( source.data == NULL || ( picX >> shift ) + planeWidth > source.width ||
			( picY >> shift ) + planeHeight > source.height ) {
			return;
		}
		// libtheora's rows may run bottom to top (a negative stride)
		const unsigned char* row = source.data + static_cast< ptrdiff_t >( picY >> shift ) * source.stride + ( picX >> shift );
		for ( int y = 0; y < planeHeight; y++ ) {
			memcpy( out, row, planeWidth );
			out += planeWidth;
			row += source.stride;
		}
	}
}

cinData_t sdSoundTheoraMovie::ImageForTime() {
	if ( !playing || state == NULL || videoBuffer == NULL ) {
		return Sound_EmptyCinematicData();
	}
	// the frame due now: frame n shows from n * denominator / numerator seconds
	const int64_t elapsed = Max( 0, Sys_Milliseconds() - startTime );
	const int64_t due = elapsed * fpsNumerator / ( static_cast< int64_t >( fpsDenominator ) * 1000 ) + 1;
	int behind = static_cast< int >( Min( due - framesDecoded, static_cast< int64_t >( MAX_CATCH_UP ) ) );
	while ( behind > 0 ) {
		if ( !DecodeNextFrame( behind == 1 ) ) {
			playing = false;
			return Sound_EmptyCinematicData();
		}
		behind--;
	}
	if ( due - framesDecoded > 0 ) {
		// too far behind to catch up: start the clock again from here
		startTime = Sys_Milliseconds() - static_cast< int >( static_cast< int64_t >( framesDecoded - 1 ) * fpsDenominator * 1000 / fpsNumerator );
	}

	cinData_t data = Sound_EmptyCinematicData();
	data.imageWidth = width;
	data.imageHeight = height;
	const int lumaSize = width * height;
	const int chromaSize = ( width >> 1 ) * ( height >> 1 );
	data.image[ 0 ] = videoBuffer;
	data.image[ 1 ] = videoBuffer + lumaSize;
	data.image[ 2 ] = videoBuffer + lumaSize + chromaSize;
	return data;
}
