#pragma once

#include <memory>
#include <AudioToolbox/AudioToolbox.h>

struct AudioStreamBasicDescription;

namespace audio2 { namespace cocoa {

void printASBD( const ::AudioStreamBasicDescription &asbd );

struct AudioBufferListDeleter {
	void operator()( ::AudioBufferList *bufferList ) { free( bufferList ); }
};

typedef std::unique_ptr<::AudioBufferList, AudioBufferListDeleter> AudioBufferListRef;

// TODO: consider adopting the CAPublicUitility way of doing this (I think it does it on the stack)
AudioBufferListRef createNonInterleavedBufferList( size_t numChannels, size_t numFrames );

::AudioComponent findAudioComponent( const ::AudioComponentDescription &componentDescription );
void findAndCreateAudioComponent( const ::AudioComponentDescription &componentDescription, ::AudioComponentInstance *componentInstance );

::AudioStreamBasicDescription interleavedFloatABSD( size_t numChannels, size_t sampleRate );
::AudioStreamBasicDescription nonInterleavedFloatABSD( size_t numChannels, size_t sampleRate );

} } // namespace audio2::cocoa