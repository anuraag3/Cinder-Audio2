#include "audio2/cocoa/Util.h"
#include "audio2/assert.h"

#include <AudioToolbox/AudioToolbox.h>

namespace audio2 { namespace cocoa {

void printASBD( const ::AudioStreamBasicDescription &asbd ) {
	char formatIDString[5];
	UInt32 formatID = CFSwapInt32HostToBig (asbd.mFormatID);
	bcopy (&formatID, formatIDString, 4);
	formatIDString[4] = '\0';

	printf( "  Sample Rate:         %10.0f\n",  asbd.mSampleRate );
	printf( "  Format ID:           %10s\n",    formatIDString );
	printf( "  Format Flags:        %10X\n",    (unsigned int)asbd.mFormatFlags );
	printf( "  Bytes per Packet:    %10d\n",    (unsigned int)asbd.mBytesPerPacket );
	printf( "  Frames per Packet:   %10d\n",    (unsigned int)asbd.mFramesPerPacket );
	printf( "  Bytes per Frame:     %10d\n",    (unsigned int)asbd.mBytesPerFrame );
	printf( "  Channels per Frame:  %10d\n",    (unsigned int)asbd.mChannelsPerFrame );
	printf( "  Bits per Channel:    %10d\n",    (unsigned int)asbd.mBitsPerChannel );
}

AudioBufferListRef createNonInterleavedBufferList( size_t numChannels, size_t channelSize )
{

	AudioBufferList *bufferList = static_cast<AudioBufferList *>( calloc( 1, sizeof(AudioBufferList) + sizeof(AudioBuffer) * (numChannels - 1) ) );
	bufferList->mNumberBuffers = numChannels;
	for( size_t i = 0; i < numChannels; i++ ) {
		bufferList->mBuffers[i].mNumberChannels = 1;
		bufferList->mBuffers[i].mDataByteSize = channelSize;
	}

	return AudioBufferListRef( bufferList );
}

AudioComponent findAudioComponent( const AudioComponentDescription &componentDescription )
{
	AudioComponent component = AudioComponentFindNext( NULL, &componentDescription );
	CI_ASSERT( component );
	return component;
}

void findAndCreateAudioComponent( const AudioComponentDescription &componentDescription, AudioComponentInstance *componentInstance )
{
	AudioComponent component = findAudioComponent( componentDescription );
	OSStatus status = AudioComponentInstanceNew( component, componentInstance );
	CI_ASSERT( status == noErr );
}

} } // namespace audio2::cocoa