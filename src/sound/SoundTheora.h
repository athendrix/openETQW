// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SOUNDTHEORA_H__
#define __SOUNDTHEORA_H__

#include "../renderer/Cinematic.h"

/*
An Ogg Theora movie (.ogv), the portable stand-in for a Bink cinematic: a sound
shader that names "video/intro.bik" plays "video/intro.ogv" when it exists
(SoundBink.cpp), on every platform, from a pak or a loose file. Its frames are
4:2:0 YUV, the planes the GUI's cinematic image takes (GuiModel.cpp).

Only the Theora stream is decoded; the sound track is a .ogg file beside the movie,
which the emitter mixes like any other sample (snd_emitter.cpp).
*/

class idFile;

class sdSoundTheoraMovie {
public:
	static const int	MAX_DIMENSION	= 4096;

						sdSoundTheoraMovie();
						~sdSoundTheoraMovie();

	// false when the file is missing or isn't a 4:2:0 Theora movie
	bool				Open( const char* moviePath );
	void				Close();
	bool				IsPlaying() const { return playing; }
	cinData_t			ImageForTime();

private:
	bool				ReadMore();
	bool				ReadHeaders();
	bool				DecodeNextFrame( bool copyOut );
	void				CopyFrame();

	struct theoraState_t*	state;
	idFile*				file;
	byte*				videoBuffer;
	int					width;
	int					height;
	int					fpsNumerator;
	int					fpsDenominator;
	int					framesDecoded;
	int					startTime;
	bool				playing;
};

#endif /* !__SOUNDTHEORA_H__ */
