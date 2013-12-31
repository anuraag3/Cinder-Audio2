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

#include "cinder/audio2/File.h"
#include "cinder/audio2/dsp/Converter.h"
#include "cinder/audio2/FileOggVorbis.h"
#include "cinder/audio2/Debug.h"

#include "cinder/Cinder.h"
#include "cinder/Utilities.h"

#if defined( CINDER_COCOA )
	#include "cinder/audio2/cocoa/FileCoreAudio.h"
#elif defined( CINDER_MSW )
	#include "cinder/audio2/msw/FileMediaFoundation.h"
#endif

using namespace std;

namespace cinder { namespace audio2 {

Source::Source()
	: mNativeSampleRate( 0 ), mNativeNumChannels( 0 ), mSampleRate( 0 ), mNumChannels( 0 ), mMaxFramesPerRead( 4096 )
{
}

Source::~Source()
{
}

SourceFile::SourceFile() : Source(), mNumFrames( 0 ), mFileNumFrames( 0 ), mReadPos( 0 )
{
}

void SourceFile::setOutputFormat( size_t outputSampleRate, size_t outputNumChannels )
{
	bool updated = false;
	if( mSampleRate != outputSampleRate ) {
		updated = true;
		mSampleRate = outputSampleRate;
	}
	if( outputNumChannels && mNumChannels != outputNumChannels ) {
		updated = true;
		mNumChannels = outputNumChannels;
	}

	if( updated ) {
		if( mSampleRate != mNativeSampleRate || mNumChannels != mNativeNumChannels ) {
			if( ! supportsConversion() ) {
				mConverter = audio2::dsp::Converter::create( mNativeSampleRate, mSampleRate, mNativeNumChannels, mNumChannels, mMaxFramesPerRead );
				mConverterReadBuffer.setSize( mMaxFramesPerRead, mNativeNumChannels );
				LOG_V( "created Converter for samplerate: " << mNativeSampleRate << " -> " << mSampleRate << ", channels: " << mNativeNumChannels << " -> " << mNumChannels << ", output num frames: " << mNumFrames );
			}

			mNumFrames = std::ceil( (float)mFileNumFrames * (float)mSampleRate / (float)mNativeSampleRate );
		}
		else {
			mNumFrames = mFileNumFrames;
			mConverter.reset();
		}

		outputFormatUpdated();
	}
}

size_t SourceFile::read( Buffer *buffer )
{
	CI_ASSERT( buffer->getNumChannels() == mNumChannels );
	CI_ASSERT( mReadPos < mNumFrames );

	size_t numRead;

	if( mConverter ) {
		size_t sourceBufFrames = buffer->getNumFrames() * (float)mNativeSampleRate / (float)mSampleRate;
		size_t numFramesNeeded = std::min( mFileNumFrames - mReadPos, std::min( mMaxFramesPerRead, sourceBufFrames ) );

		mConverterReadBuffer.setNumFrames( numFramesNeeded );
		performRead( &mConverterReadBuffer, 0, numFramesNeeded );
		pair<size_t, size_t> count = mConverter->convert( &mConverterReadBuffer, buffer );
		numRead = count.second;
	}
	else {
		size_t numFramesNeeded = std::min( mNumFrames - mReadPos, std::min( mMaxFramesPerRead, buffer->getNumFrames() ) );
		numRead = performRead( buffer, 0, numFramesNeeded );
	}

	mReadPos += numRead;
	return numRead;
}

BufferRef SourceFile::loadBuffer()
{
	seek( 0 );

	BufferRef result = make_shared<Buffer>( mNumFrames, mNumChannels );

	if( mConverter ) {
		// TODO: need BufferView's in order to reduce number of copies
		Buffer destBuffer( mConverter->getDestMaxFramesPerBlock(), mNumChannels );
		size_t readCount = 0;
		while( true ) {
			size_t framesNeeded = min( mMaxFramesPerRead, mFileNumFrames - readCount );
			if( framesNeeded == 0 )
				break;

			// make sourceBuffer num frames match outNumFrames so that Converter doesn't think it has more
			if( framesNeeded < mConverterReadBuffer.getNumFrames() )
				mConverterReadBuffer.setNumFrames( framesNeeded );

			size_t outNumFrames = performRead( &mConverterReadBuffer, 0, framesNeeded );
			CI_ASSERT( outNumFrames == framesNeeded );

			pair<size_t, size_t> count = mConverter->convert( &mConverterReadBuffer, &destBuffer );

			for( int ch = 0; ch < mNumChannels; ch++ ) {
				float *channel = destBuffer.getChannel( ch );
				copy( channel, channel + count.second, result->getChannel( ch ) + mReadPos );
			}

			readCount += outNumFrames;
			mReadPos += count.second;
		}
	}
	else {
		size_t readCount = performRead( result.get(), 0, mNumFrames );
		mReadPos = readCount;
	}

	return result;
}

void SourceFile::seek( size_t readPositionFrames )
{
	if( readPositionFrames >= mNumFrames )
		return;

	// adjust read pos for samplerate conversion so that it is relative to file num frames
	size_t fileReadPos = readPositionFrames;
	if( mSampleRate != mNativeSampleRate )
		fileReadPos *= (float)mFileNumFrames / (float)mNumFrames;

	performSeek( fileReadPos );
	mReadPos = readPositionFrames;
}

// TODO: these should be replaced with a generic registrar derived from the ImageIo stuff.

std::unique_ptr<SourceFile> SourceFile::create( const DataSourceRef &dataSource )
{
	if( getPathExtension( dataSource->getFilePathHint() ) == "ogg" )
		return std::unique_ptr<SourceFile>( new SourceFileOggVorbis( dataSource ) );

#if defined( CINDER_COCOA )
	return std::unique_ptr<SourceFile>( new cocoa::SourceFileCoreAudio( dataSource ) );
#elif defined( CINDER_MSW )
	return std::unique_ptr<SourceFile>( new msw::SourceFileMediaFoundation( dataSource ) );
#endif
}

std::unique_ptr<TargetFile> TargetFile::create( const DataTargetRef &dataTarget, size_t sampleRate, size_t numChannels, const std::string &extension )
{
	std::string ext = ( ! extension.empty() ? extension : getPathExtension( dataTarget->getFilePathHint() ) );

#if defined( CINDER_COCOA )
	return std::unique_ptr<TargetFile>( new cocoa::TargetFileCoreAudio( dataTarget, sampleRate, numChannels, ext ) );
#elif defined( CINDER_MSW )
	return std::unique_ptr<TargetFile>(); // TODO
//	return std::unique_ptr<TargetFile>( new msw::TargetFileMediaFoundation( dataTarget, sampleRate, numChannels, ext ) );
#endif
}

std::unique_ptr<TargetFile> TargetFile::create( const fs::path &path, size_t sampleRate, size_t numChannels, const std::string &extension )
{
	return create( (DataTargetRef)writeFile( path ), sampleRate, numChannels, extension );
}

} } // namespace cinder::audio2