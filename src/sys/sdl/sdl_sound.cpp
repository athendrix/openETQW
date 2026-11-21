// Copyright (C) 2007 Id Software, Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework/precompiled.h"
#include <SDL3/SDL.h>
#include "sound/AudioHardware.h"

extern idCVar s_primaryDevice;	// snd_system.cpp: the options menu's playback device (its hash), empty for the default

/*
The playback devices SDL sees, by name, for the options menu's list (idSoundSystemLocal::
ListSoundPlaybackDevices). Needs SDL's audio subsystem, which the sound hardware starts.
*/
void Sys_ListAudioPlaybackDevices( idWStrList& names ) {
	names.Clear();
	if ( SDL_WasInit( SDL_INIT_AUDIO ) == 0 ) {
		return;
	}
	int count = 0;
	SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices( &count );
	if ( devices == NULL ) {
		return;
	}
	for ( int i = 0; i < count; i++ ) {
		const char* name = SDL_GetAudioDeviceName( devices[ i ] );
		if ( name != NULL && name[ 0 ] != '\0' ) {
			idWStr wide;
			wide = va( L"%hs", name );
			names.Append( wide );
		}
	}
	SDL_free( devices );
}

namespace {

/*
The device s_primaryDevice names: the menu stores a device's hash (as
idSoundSystem::GetAudioDeviceHash makes it from the name), a config may hold the name
itself. Not found, or empty: the system's default device.
*/
SDL_AudioDeviceID ChosenPlaybackDevice() {
	const char* wanted = s_primaryDevice.GetString();
	if ( wanted == NULL || wanted[ 0 ] == '\0' || soundSystem == NULL ) {
		return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
	}
	const bool isHash = idStr::IsNumeric( wanted );
	const int wantedHash = isHash ? atoi( wanted ) : 0;
	int count = 0;
	SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices( &count );
	SDL_AudioDeviceID chosen = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
	for ( int i = 0; devices != NULL && i < count; i++ ) {
		const char* name = SDL_GetAudioDeviceName( devices[ i ] );
		if ( name == NULL ) {
			continue;
		}
		idWStr wide;
		wide = va( L"%hs", name );
		if ( ( isHash && soundSystem->GetAudioDeviceHash( wide.c_str() ) == wantedHash ) || ( !isHash && idStr::Icmp( name, wanted ) == 0 ) ) {
			chosen = devices[ i ];
			common->Printf( "SDL3: playback device '%s'\n", name );
			break;
		}
	}
	if ( devices != NULL ) {
		SDL_free( devices );
	}
	if ( chosen == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ) {
		common->Printf( "SDL3: playback device '%s' not found; using the default\n", wanted );
	}
	return chosen;
}

class idAudioHardwareSDL3 : public idAudioHardware {
public:
	idAudioHardwareSDL3() :
		audioStream( NULL ),
		numSpeakers( 2 ),
		sampleRate( 44100 ),
		mixBuffer( NULL ),
		mixBufferSize( 0 ) {
	}

	virtual ~idAudioHardwareSDL3() {
		if ( audioStream != NULL ) {
			SDL_DestroyAudioStream( audioStream );
			audioStream = NULL;
		}
		if ( mixBuffer != NULL ) {
			delete[] mixBuffer;
			mixBuffer = NULL;
		}
	}

	virtual bool Initialize() override {
		if ( !SDL_InitSubSystem( SDL_INIT_AUDIO ) ) {
			common->Warning( "SDL3: failed to initialize audio subsystem: %s", SDL_GetError() );
			return false;
		}

		numSpeakers = 2;
		sampleRate = 44100;

		SDL_AudioSpec spec;
		memset( &spec, 0, sizeof( spec ) );
		spec.format = SDL_AUDIO_S16;
		spec.channels = numSpeakers;
		spec.freq = sampleRate;

		audioStream = SDL_OpenAudioDeviceStream(
			ChosenPlaybackDevice(),
			&spec,
			NULL,
			NULL
		);
		if ( audioStream == NULL && s_primaryDevice.GetString()[ 0 ] != '\0' ) {
			common->Warning( "SDL3: couldn't open the chosen playback device (%s); trying the default", SDL_GetError() );
			audioStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL );
		}
		if ( audioStream == NULL ) {
			common->Warning( "SDL3: failed to open audio stream: %s", SDL_GetError() );
			return false;
		}

		mixBufferSize = MIXBUFFER_SAMPLES * numSpeakers * sizeof( short );
		mixBuffer = new short[ MIXBUFFER_SAMPLES * numSpeakers ];
		memset( mixBuffer, 0, mixBufferSize );

		if ( !SDL_ResumeAudioStreamDevice( audioStream ) ) {
			common->Warning( "SDL3: failed to resume audio device: %s", SDL_GetError() );
			return false;
		}

		common->Printf( "SDL3 Audio initialized: %d Hz, %d channels, 16-bit PCM\n", sampleRate, numSpeakers );
		return true;
	}

	virtual bool Lock( void **pDSLockedBuffer, ulong *dwDSLockedBufferSize ) override {
		(void)pDSLockedBuffer;
		(void)dwDSLockedBufferSize;
		return false;
	}

	virtual bool Unlock( void *pDSLockedBuffer, dword dwDSLockedBufferSize ) override {
		(void)pDSLockedBuffer;
		(void)dwDSLockedBufferSize;
		return false;
	}

	virtual bool GetCurrentPosition( ulong *pdwCurrentWriteCursor ) override {
		(void)pdwCurrentWriteCursor;
		return false;
	}

	virtual bool Flush( void ) override {
		return true;
	}

	// The caller writes on its own schedule, not when the device needs data: Windows' sound
	// thread writes one mix buffer every USERCMD_MSEC, about three times what 44.1 kHz plays,
	// and a device that has gone away (a disconnected remote session) plays nothing. SDL
	// keeps all that is put, so the queue grew by 18-28 MB a minute (#367). Only queue
	// while less than MAX_QUEUED_MIX_BUFFERS are waiting. Today the buffer is always
	// silence (the bootstrap sound system mixes nothing). A real mixer should check this
	// gate before it mixes, or the buffers dropped here would be lost audio.
	virtual void Write( bool flushing ) override {
		(void)flushing;
		if ( audioStream == NULL || mixBuffer == NULL ) {
			return;
		}
		const int queued = SDL_GetAudioStreamQueued( audioStream );
		if ( queued < 0 || queued >= MAX_QUEUED_MIX_BUFFERS * mixBufferSize ) {
			return;
		}
		SDL_PutAudioStreamData( audioStream, mixBuffer, mixBufferSize );
	}

	virtual int GetNumberOfSpeakers( void ) override {
		return numSpeakers;
	}

	virtual int GetMixBufferSize( void ) override {
		return mixBufferSize;
	}

	virtual short* GetMixBuffer( void ) override {
		return mixBuffer;
	}

private:
	static const int	MAX_QUEUED_MIX_BUFFERS = 2;

	SDL_AudioStream*	audioStream;
	int					numSpeakers;
	int					sampleRate;
	short*				mixBuffer;
	int					mixBufferSize;
};

} // namespace

idAudioHardware *idAudioHardware::Alloc() {
	return new idAudioHardwareSDL3();
}

idAudioHardware::~idAudioHardware() {
}
