/*
 Copyright (c) 2014, The Cinder Project

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

#include "cinder/audio2/NodeOutput.h"
#include "cinder/audio2/Context.h"
#include "cinder/audio2/Utilities.h"
#include "cinder/audio2/Exception.h"
#include "cinder/audio2/Debug.h"

using namespace std;

namespace cinder { namespace audio2 {

// ----------------------------------------------------------------------------------------------------
// MARK: - NodeOutput
// ----------------------------------------------------------------------------------------------------

NodeOutput::NodeOutput( const Format &format )
	: Node( format ), mNumProcessedFrames( 0 ), mClipDetectionEnabled( true ), mClipThreshold( 2 ), mLastClip( 0 )
{
}

void NodeOutput::connect( const NodeRef &output, size_t outputBus, size_t inputBus )
{
	CI_ASSERT_MSG( 0, "NodeOutput does not support outputs" );
}

void NodeOutput::postProcess()
{
	getContext()->processAutoPulledNodes();
	incrementFrameCount();
}

uint64_t NodeOutput::getLastClip()
{
	uint64_t result = mLastClip;
	mLastClip = 0;
	return result;
}

void NodeOutput::enableClipDetection( bool enable, float threshold )
{
	lock_guard<mutex> lock( getContext()->getMutex() );

	mClipDetectionEnabled = enable;
	mClipThreshold = threshold;
}

bool NodeOutput::checkNotClipping()
{
	if( mClipDetectionEnabled ) {
		size_t recordedFrame;
		if( thresholdBuffer( mInternalBuffer, mClipThreshold, &recordedFrame ) ) {
			mLastClip = getNumProcessedFrames() + recordedFrame;
			return true;
		}
	}

	return false;
}

void NodeOutput::incrementFrameCount()
{
	mNumProcessedFrames += getOutputFramesPerBlock();
}

// ----------------------------------------------------------------------------------------------------
// MARK: - LineOut
// ----------------------------------------------------------------------------------------------------

LineOut::LineOut( const DeviceRef &device, const Format &format )
	: NodeOutput( format ), mDevice( device )
{
	CI_ASSERT( mDevice );

	mWillChangeConn = mDevice->getSignalParamsWillChange().connect( bind( &LineOut::deviceParamsWillChange, this ) );
	mDidChangeConn = mDevice->getSignalParamsDidChange().connect( bind( &LineOut::deviceParamsDidChange, this ) );

	size_t deviceNumChannels = mDevice->getNumOutputChannels();

	if( mChannelMode != ChannelMode::SPECIFIED ) {
		mChannelMode = ChannelMode::SPECIFIED;
		setNumChannels( std::min( deviceNumChannels, (size_t)2 ) );
	}

	if( deviceNumChannels < mNumChannels )
		throw AudioFormatExc( string( "Device can not accommodate " ) + to_string( deviceNumChannels ) + " output channels." );
}

void LineOut::deviceParamsWillChange()
{
	mWasEnabledBeforeParamsChange = mEnabled;

	getContext()->stop();
	getContext()->uninitializeAllNodes();
}

void LineOut::deviceParamsDidChange()
{
	getContext()->initializeAllNodes();

	getContext()->setEnabled( mWasEnabledBeforeParamsChange );
}

} } // namespace cinder::audio2