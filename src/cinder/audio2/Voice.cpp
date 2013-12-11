/*
 Copyright (c) 2013, The Cinder Project

 This code is intended to be used with the Cinder C++ library, http://libcinder.org

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and
	the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
	the following disclaimer in the documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
*/

#include "cinder/audio2/Voice.h"
#include "cinder/audio2/Context.h"
#include "cinder/audio2/NodeEffect.h"

#include <map>

using namespace std;
using namespace ci;

namespace cinder { namespace audio2 {

// ----------------------------------------------------------------------------------------------------
// MARK: - MixerImpl
// ----------------------------------------------------------------------------------------------------

// TODO: replace this private Mixer, which composits a Gain + NodePan per voice, into a NodeMixer
// that has a custom pullInputs and performs that gain / pan as a post-processing step

class MixerImpl {
public:

	static MixerImpl *get();

	//! returns the number of connected busses.
	void	setBusVolume( size_t busId, float volume );
	float	getBusVolume( size_t busId );
	void	setBusPan( size_t busId, float pos );
	float	getBusPan( size_t busId );

	void	addVoice( const VoiceRef &source );

	BufferRef loadSourceFile( const SourceFileRef &sourceFile );

private:
	MixerImpl();

	struct Bus {
		VoiceRef		mVoice;
		GainRef		mGain;
		Pan2dRef	mPan;
	};

	std::vector<Bus> mBusses;
	std::map<SourceFileRef, BufferRef> mBufferCache;

	GainRef mMasterGain;
};

MixerImpl* MixerImpl::get()
{
	static unique_ptr<MixerImpl> sMixer;
	if( ! sMixer )
		sMixer.reset( new MixerImpl );

	return sMixer.get();
}

MixerImpl::MixerImpl()
{
	Context *ctx = Context::master();
	mMasterGain = ctx->makeNode( new Gain() );

	mMasterGain->addConnection( ctx->getTarget() );

	ctx->start();
}

void MixerImpl::addVoice( const VoiceRef &source )
{
	Context *ctx = Context::master();

	source->mBusId = mBusses.size();
	mBusses.push_back( MixerImpl::Bus() );
	MixerImpl::Bus &bus = mBusses.back();

	bus.mVoice = source;
	bus.mGain = ctx->makeNode( new Gain() );
	bus.mPan = ctx->makeNode( new Pan2d() );

	source->getNode()->connect( bus.mGain )->connect( bus.mPan )->connect( mMasterGain );
}

BufferRef MixerImpl::loadSourceFile( const SourceFileRef &sourceFile )
{
	auto cached = mBufferCache.find( sourceFile );
	if( cached != mBufferCache.end() )
		return cached->second;
	else {
		BufferRef result = sourceFile->loadBuffer();
		mBufferCache.insert( make_pair( sourceFile, result ) );
		return result;
	}
}

void MixerImpl::setBusVolume( size_t busId, float volume )
{
	mBusses[busId].mGain->setValue( volume );
}

float MixerImpl::getBusVolume( size_t busId )
{
	return mBusses[busId].mGain->getValue();
}

void MixerImpl::setBusPan( size_t busId, float pos )
{
	auto pan = mBusses[busId].mPan;
	if( pan )
		pan->setPos( pos );
}

float MixerImpl::getBusPan( size_t busId )
{
	auto pan = mBusses[busId].mPan;
	if( pan )
		return mBusses[busId].mPan->getPos();

	return 0.0f;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - Voice
// ----------------------------------------------------------------------------------------------------

VoiceRef Voice::create( CallbackProcessorFn callbackFn )
{
	VoiceRef result( new VoiceCallbackProcessor( callbackFn ) );
	MixerImpl::get()->addVoice( result );

	return result;
}

VoiceSamplePlayerRef Voice::create( const SourceFileRef &sourceFile )
{
	VoiceSamplePlayerRef result( new VoiceSamplePlayer( sourceFile ) );
	MixerImpl::get()->addVoice( result );

	return result;
}

void Voice::setVolume( float volume )
{
	MixerImpl::get()->setBusVolume( mBusId, volume );
}

void Voice::setPan( float pan )
{
	MixerImpl::get()->setBusPan( mBusId, pan );
}

void play( const VoiceRef &source )
{
	source->getNode()->start();
}

// ----------------------------------------------------------------------------------------------------
// MARK: - VoiceSamplePlayer
// ----------------------------------------------------------------------------------------------------

VoiceSamplePlayer::VoiceSamplePlayer( const SourceFileRef &sourceFile )
{
	// maximum samples for default buffer playback is 1 second stereo at 48k samplerate
	const size_t kMaxFramesForBufferPlayback = 48000 * 2;

	if( sourceFile->getNumFrames() < kMaxFramesForBufferPlayback ) {
		BufferRef buffer = MixerImpl::get()->loadSourceFile( sourceFile ); // TODO: cache buffer
		mNode = Context::master()->makeNode( new BufferPlayer( buffer ) );
	} else
		mNode = Context::master()->makeNode( new FilePlayer( sourceFile ) );
}

// ----------------------------------------------------------------------------------------------------
// MARK: - VoiceCallbackProcessor
// ----------------------------------------------------------------------------------------------------

// TODO: how best to specify channel count?
VoiceCallbackProcessor::VoiceCallbackProcessor( const CallbackProcessorFn &callbackFn )
{
	mNode = Context::master()->makeNode( new CallbackProcessor( callbackFn ) );
//	mNode = Context::master()->makeNode( new CallbackProcessor( callbackFn, Node::Format().channels( 2 ) ) );
}


} } // namespace cinder::audio2