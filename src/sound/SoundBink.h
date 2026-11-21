// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SOUNDBINK_H__
#define __SOUNDBINK_H__

#include "../renderer/Cinematic.h"
#include "SoundTheora.h"

class idSoundShader;

/*
Bink cinematics that sound shaders name: the GUI movies (#190). An emitter whose
shader names a .bik sample plays its frames through ImageForTime (#436).

The .ogv beside the .bik (same name), when there is one, plays instead on every
platform (SoundTheora.h); the .ogg beside it is its sound track. binkw32.dll is only
tried on Windows, when there is no .ogv.
*/

// a cinData_t with no image
cinData_t Sound_EmptyCinematicData();

// the .bik sample the shader names, if any
bool Sound_FindBinkPath( const idSoundShader* shader, idStr& samplePath );

class sdSoundBinkMovie {
public:
					sdSoundBinkMovie();
					~sdSoundBinkMovie();

	bool			Open( const char* samplePath );
	void			Close();
	bool			IsPlaying() const;
	cinData_t		ImageForTime();
	// the .ogg sound track of the movie Open played as Theora; empty when none
	const char*		SoundTrack() const { return soundTrack.c_str(); }

private:
	bool			DecodeFrame();

	void*			bink;
	byte*			videoBuffer;
	int				width;
	int				height;
	int				frameCount;
	int				framesDecoded;
	int				frameMsec;
	int				nextFrameTime;
	bool			playing;
	bool			frameReady;
	sdSoundTheoraMovie	theora;
	bool			useTheora;
	idStr			soundTrack;
};

#endif /* !__SOUNDBINK_H__ */
