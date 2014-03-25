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

#if( _WIN32_WINNT >= _WIN32_WINNT_VISTA )

#include "cinder/audio2/msw/ContextWasapi.h"
#include "cinder/audio2/msw/DeviceManagerWasapi.h"
#include "cinder/audio2/msw/MswUtil.h"
#include "cinder/audio2/Context.h"
#include "cinder/audio2/dsp/Dsp.h"
#include "cinder/audio2/dsp/RingBuffer.h"
#include "cinder/audio2/dsp/Converter.h"
#include "cinder/audio2/Exception.h"
#include "cinder/audio2/CinderAssert.h"
#include "cinder/audio2/Debug.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")

// TODO: should requestedDuration come from Device's frames per block?
// - seems like this is fixed at 10ms in shared mode. (896 samples @ stereo 44100 s/r) 
#define DEFAULT_AUDIOCLIENT_FRAMES 1024

using namespace std;

namespace cinder { namespace audio2 { namespace msw {

// converts to 100-nanoseconds
inline ::REFERENCE_TIME samplesToReferenceTime( size_t samples, size_t sampleRate )
{
	return (::REFERENCE_TIME)( (double)samples * 10000000.0 / (double)sampleRate );
}

struct WasapiAudioClientImpl {
	WasapiAudioClientImpl();

	unique_ptr<::IAudioClient, ComReleaser>			mAudioClient;
	unique_ptr<dsp::RingBuffer>						mRingBuffer;

	size_t	mNumFramesBuffered, mAudioClientNumFrames, mNumChannels;

  protected:
	void initAudioClient( const DeviceRef &device, HANDLE eventHandle );
};

struct WasapiRenderClientImpl : public WasapiAudioClientImpl {
	WasapiRenderClientImpl( LineOutWasapi *lineOut );
	~WasapiRenderClientImpl();

	void init();
	void uninit();

	unique_ptr<::IAudioRenderClient, ComReleaser>	mRenderClient;

	::HANDLE	mRenderSamplesReadyEvent, mRenderShouldQuitEvent;
	::HANDLE    mRenderThread;

private:
	void initRenderClient();
	void runRenderThread();
	void renderAudio();
	void increaseThreadPriority();

	static DWORD __stdcall renderThreadEntryPoint( LPVOID Context );

	LineOutWasapi*	mLineOut; // weak pointer to parent
};

struct WasapiCaptureClientImpl : public WasapiAudioClientImpl {
	WasapiCaptureClientImpl( LineInWasapi *lineOut );

	void init();
	void uninit();
	void captureAudio();

	unique_ptr<::IAudioCaptureClient, ComReleaser>	mCaptureClient;

  private:
	  void initCapture();

	  LineInWasapi*		mLineIn; // weak pointer to parent
};

// ----------------------------------------------------------------------------------------------------
// MARK: - WasapiAudioClientImpl
// ----------------------------------------------------------------------------------------------------

WasapiAudioClientImpl::WasapiAudioClientImpl()
	: mAudioClientNumFrames( DEFAULT_AUDIOCLIENT_FRAMES ), mNumFramesBuffered( 0 ), mNumChannels( 0 )
{
}

void WasapiAudioClientImpl::initAudioClient( const DeviceRef &device, HANDLE eventHandle )
{
	CI_ASSERT( ! mAudioClient );

	DeviceManagerWasapi *manager = dynamic_cast<DeviceManagerWasapi *>( Context::deviceManager() );
	CI_ASSERT( manager );

	//auto device = mLineIn->getDevice();
	shared_ptr<::IMMDevice> immDevice = manager->getIMMDevice( device );

	::IAudioClient *audioClient;
	HRESULT hr = immDevice->Activate( __uuidof(::IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient );
	CI_ASSERT( hr == S_OK );
	mAudioClient = makeComUnique( audioClient );

	// set default format to match the audio client's defaults

	::WAVEFORMATEX *mixFormat;
	hr = mAudioClient->GetMixFormat( &mixFormat );
	CI_ASSERT( hr == S_OK );

	// TODO: sort out how to specify channels
	// - count
	// - index (ex 4, 5, 8, 9)
	mNumChannels = mixFormat->nChannels;
	::CoTaskMemFree( mixFormat );

	size_t sampleRate = device->getSampleRate();
	
	auto wfx = interleavedFloatWaveFormat( sampleRate, mNumChannels );
	::WAVEFORMATEX *closestMatch;
	hr = mAudioClient->IsFormatSupported( ::AUDCLNT_SHAREMODE_SHARED, wfx.get(), &closestMatch );
	if( hr == S_FALSE ) {
		CI_LOG_E( "cannot use requested format. TODO: use closestMatch" );
		CI_ASSERT( closestMatch );
	}
	else if( hr != S_OK )
		throw AudioFormatExc( "Could not find a suitable format for IAudioClient" );

	::REFERENCE_TIME requestedDuration = samplesToReferenceTime( mAudioClientNumFrames, sampleRate );
	DWORD streamFlags = eventHandle ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0;

	hr = mAudioClient->Initialize( ::AUDCLNT_SHAREMODE_SHARED, streamFlags, requestedDuration, 0, wfx.get(), NULL ); 
	CI_ASSERT( hr == S_OK );

	if( eventHandle ) {
		// enable event driven rendering.
		HRESULT hr = mAudioClient->SetEventHandle( eventHandle );
		CI_ASSERT( hr == S_OK );
	}

	UINT32 actualNumFrames;
	hr = mAudioClient->GetBufferSize( &actualNumFrames );
	CI_ASSERT( hr == S_OK );

	mAudioClientNumFrames = actualNumFrames; // update with the actual size
}

// ----------------------------------------------------------------------------------------------------
// MARK: - WasapiRenderClientImpl
// ----------------------------------------------------------------------------------------------------

WasapiRenderClientImpl::WasapiRenderClientImpl( LineOutWasapi *lineOut )
	: WasapiAudioClientImpl(), mLineOut( lineOut )
{
	// create render events
	mRenderSamplesReadyEvent = ::CreateEvent( NULL, FALSE, FALSE, NULL );
	CI_ASSERT( mRenderSamplesReadyEvent );

	mRenderShouldQuitEvent = ::CreateEvent( NULL, FALSE, FALSE, NULL );
	CI_ASSERT( mRenderShouldQuitEvent );
}

WasapiRenderClientImpl::~WasapiRenderClientImpl()
{
	if( mRenderSamplesReadyEvent ) {
		BOOL success = ::CloseHandle( mRenderSamplesReadyEvent );
		CI_ASSERT( success );
	}
	if( mRenderShouldQuitEvent ) {
		BOOL success = ::CloseHandle( mRenderShouldQuitEvent );
		CI_ASSERT( success );
	}
}

void WasapiRenderClientImpl::init()
{
	// reset events in case there are in a signaled state from a previous use.
	BOOL success = ::ResetEvent( mRenderShouldQuitEvent );
	CI_ASSERT( success );
	success = ::ResetEvent( mRenderSamplesReadyEvent );
	CI_ASSERT( success );

	initAudioClient( mLineOut->getDevice(), mRenderSamplesReadyEvent );
	initRenderClient();
}

void WasapiRenderClientImpl::uninit()
{
	// signal quit event and join the render thread once it completes.
	BOOL success = ::SetEvent( mRenderShouldQuitEvent );
	CI_ASSERT( success );

	::WaitForSingleObject( mRenderThread, INFINITE );
	CloseHandle( mRenderThread );
	mRenderThread = NULL;

	// Release() IAudioRenderClient IAudioClient
	mRenderClient.reset();
	mAudioClient.reset();
}

void WasapiRenderClientImpl::initRenderClient()
{
	CI_ASSERT( mAudioClient );

	::IAudioRenderClient *renderClient;
	HRESULT hr = mAudioClient->GetService( __uuidof(::IAudioRenderClient), (void**)&renderClient );
	CI_ASSERT( hr == S_OK );
	mRenderClient = makeComUnique( renderClient );

	mRingBuffer.reset( new dsp::RingBuffer( mAudioClientNumFrames * mNumChannels ) );

	mRenderThread = ::CreateThread( NULL, 0, renderThreadEntryPoint, this, 0, NULL );
	CI_ASSERT( mRenderThread );
}

// static
DWORD WasapiRenderClientImpl::renderThreadEntryPoint( ::LPVOID lpParameter )
{
	WasapiRenderClientImpl *renderer = static_cast<WasapiRenderClientImpl *>( lpParameter );
	renderer->runRenderThread();

	return 0;
}

void WasapiRenderClientImpl::runRenderThread()
{
	increaseThreadPriority();

	HANDLE waitEvents[2] = { mRenderShouldQuitEvent, mRenderSamplesReadyEvent };
	bool running = true;

	while( running ) {
		DWORD waitResult = ::WaitForMultipleObjects( 2, waitEvents, FALSE, INFINITE );
		switch( waitResult ) {
			case WAIT_OBJECT_0 + 0:     // mRenderShouldQuitEvent
				running = false;
				break;
			case WAIT_OBJECT_0 + 1:		// mRenderSamplesReadyEvent
				renderAudio();
				break;
			default:
				CI_ASSERT_NOT_REACHABLE();
		}
	}
}

void WasapiRenderClientImpl::renderAudio()
{
	// the current padding represents the number of frames queued on the audio endpoint, waiting to be sent to hardware.
	UINT32 numFramesPadding;
	HRESULT hr = mAudioClient->GetCurrentPadding( &numFramesPadding );
	CI_ASSERT( hr == S_OK );

	size_t numWriteFramesAvailable = mAudioClientNumFrames - numFramesPadding;

	while( mNumFramesBuffered < numWriteFramesAvailable )
		mLineOut->renderInputs();

	float *renderBuffer;
	hr = mRenderClient->GetBuffer( numWriteFramesAvailable, (BYTE **)&renderBuffer );
	CI_ASSERT( hr == S_OK );

	DWORD bufferFlags = 0;
	size_t numReadSamples = numWriteFramesAvailable * mNumChannels;
	bool readSuccess = mRingBuffer->read( renderBuffer, numReadSamples );
	CI_ASSERT( readSuccess );
	mNumFramesBuffered -= numWriteFramesAvailable;

	hr = mRenderClient->ReleaseBuffer( numWriteFramesAvailable, bufferFlags );
	CI_ASSERT( hr == S_OK );
}

// This uses the "Multimedia Class Scheduler Service" (MMCSS) to increase the priority of the current thread.
// The priority increase can be seen in the threads debugger, it should have Priority = "Time Critical"
void WasapiRenderClientImpl::increaseThreadPriority()
{
	DWORD taskIndex = 0;
	::HANDLE avrtHandle = ::AvSetMmThreadCharacteristics( L"Pro Audio", &taskIndex );
	if( ! avrtHandle )
		CI_LOG_W( "Unable to enable MMCSS for 'Pro Audio', error: " << GetLastError() );
}

// ----------------------------------------------------------------------------------------------------
// MARK: - WasapiCaptureClientImpl
// ----------------------------------------------------------------------------------------------------

WasapiCaptureClientImpl::WasapiCaptureClientImpl( LineInWasapi *lineIn )
	: WasapiAudioClientImpl(), mLineIn( lineIn )
{
}

void WasapiCaptureClientImpl::init()
{
	initAudioClient( mLineIn->getDevice(), nullptr );
	initCapture();
}

void WasapiCaptureClientImpl::uninit()
{
	// Release() the maintained IAudioCaptureClient and IAudioClient
	mCaptureClient.reset();
	mAudioClient.reset();
}

void WasapiCaptureClientImpl::initCapture()
{
	CI_ASSERT( mAudioClient );

	::IAudioCaptureClient *captureClient;
	HRESULT hr = mAudioClient->GetService( __uuidof(::IAudioCaptureClient), (void**)&captureClient );
	CI_ASSERT( hr == S_OK );
	mCaptureClient = makeComUnique( captureClient );

	mRingBuffer.reset( new dsp::RingBuffer( mAudioClientNumFrames * mNumChannels ) );
}

void WasapiCaptureClientImpl::captureAudio()
{
	UINT32 sizeNextPacket;
	HRESULT hr = mCaptureClient->GetNextPacketSize( &sizeNextPacket ); // TODO: treat this accordingly for stereo (2x)
	CI_ASSERT( hr == S_OK );

	while( sizeNextPacket ) {
		if( ( sizeNextPacket ) > ( mAudioClientNumFrames - mNumFramesBuffered ) )
			return; // not enough space, we'll read it next time
	
		BYTE *audioData;
		UINT32 numFramesAvailable;
		DWORD flags;
		HRESULT hr = mCaptureClient->GetBuffer( &audioData, &numFramesAvailable, &flags, NULL, NULL );
		if( hr == AUDCLNT_S_BUFFER_EMPTY )
			return;
		else
			CI_ASSERT( hr == S_OK );

		if ( flags & AUDCLNT_BUFFERFLAGS_SILENT ) {
			CI_LOG_W( "silence. TODO: fill buffer with zeros." );
			// ???: ignore this? copying the samples is just about the same as setting to 0
			//fill( mCaptureBuffer.begin(), mCaptureBuffer.end(), 0.0f );
		}
		else {
			float *samples = (float *)audioData;
			size_t numSamples = numFramesAvailable * mNumChannels;
			if( ! mRingBuffer->write( samples, numSamples ) )
				mLineIn->markOverrun();

			mNumFramesBuffered += static_cast<size_t>( numFramesAvailable );
		}

		hr = mCaptureClient->ReleaseBuffer( numFramesAvailable );
		CI_ASSERT( hr == S_OK );

		hr = mCaptureClient->GetNextPacketSize( &sizeNextPacket );
		CI_ASSERT( hr == S_OK );
	}
}

// ----------------------------------------------------------------------------------------------------
// MARK: - LineOutWasapi
// ----------------------------------------------------------------------------------------------------

LineOutWasapi::LineOutWasapi( const DeviceRef &device, const Format &format )
	: LineOut( device, format ), mRenderImpl( new WasapiRenderClientImpl( this ) )
{
}

void LineOutWasapi::initialize()
{
	setupProcessWithSumming();
	mInterleavedBuffer = BufferInterleaved( getFramesPerBlock(), mNumChannels );

	mRenderImpl->init();
}

void LineOutWasapi::uninitialize()
{
	mRenderImpl->uninit();
}

void LineOutWasapi::start()
{
	if( ! mInitialized ) {
		CI_LOG_E( "not initialized" );
		return;
	}

	mEnabled = true;
	HRESULT hr = mRenderImpl->mAudioClient->Start();
	CI_ASSERT( hr == S_OK );
}

void LineOutWasapi::stop()
{
	if( ! mInitialized ) {
		CI_LOG_E( "not initialized" );
		return;
	}

	HRESULT hr = mRenderImpl->mAudioClient->Stop();
	CI_ASSERT( hr == S_OK );

	mEnabled = false;
}

void LineOutWasapi::renderInputs()
{
	auto ctx = getContext();
	if( ! ctx )
		return;

	lock_guard<mutex> lock( ctx->getMutex() );

	// verify context still exists, since its destructor may have been holding the lock
	ctx = getContext();
	if( ! ctx )
		return;

	mInternalBuffer.zero();
	pullInputs( &mInternalBuffer );

	if( checkNotClipping() )
		mInternalBuffer.zero();

	dsp::interleaveStereoBuffer( &mInternalBuffer, &mInterleavedBuffer );
	bool writeSuccess = mRenderImpl->mRingBuffer->write( mInterleavedBuffer.getData(), mInterleavedBuffer.getSize() );
	CI_ASSERT( writeSuccess );
	mRenderImpl->mNumFramesBuffered += mInterleavedBuffer.getNumFrames();

	postProcess();
}

// ----------------------------------------------------------------------------------------------------
// MARK: - LineInWasapi
// ----------------------------------------------------------------------------------------------------

LineInWasapi::LineInWasapi( const DeviceRef &device, const Format &format )
	: LineIn( device, format ), mCaptureImpl( new WasapiCaptureClientImpl( this ) )
{
}

LineInWasapi::~LineInWasapi()
{
}

void LineInWasapi::initialize()
{
	mCaptureImpl->init();
	mInterleavedBuffer = BufferInterleaved( mCaptureImpl->mAudioClientNumFrames, mNumChannels );

	if( getDevice()->getSampleRate() != getSampleRate() ) {
		const size_t maxFramesPerRead = mInterleavedBuffer.getNumFrames();
		mConverter = audio2::dsp::Converter::create( getDevice()->getSampleRate(), getSampleRate(), getDevice()->getNumInputChannels(), getNumChannels(), maxFramesPerRead );
		mConverterReadBuffer.setSize( maxFramesPerRead, getDevice()->getNumInputChannels() );
		CI_LOG_V( "created Converter for samplerate: " << mConverter->getSourceSampleRate() << " -> " << mConverter->getDestSampleRate() << ", channels: " << mConverter->getSourceNumChannels() << " -> " << mConverter->getDestNumChannels() );
	}
}

void LineInWasapi::uninitialize()
{
	mCaptureImpl->uninit();
}

void LineInWasapi::start()
{
	HRESULT hr = mCaptureImpl->mAudioClient->Start();
	CI_ASSERT( hr == S_OK );

	mEnabled = true;
}

void LineInWasapi::stop()
{
	HRESULT hr = mCaptureImpl->mAudioClient->Stop();
	CI_ASSERT( hr == S_OK );

	mEnabled = false;
}

void LineInWasapi::process( Buffer *buffer )
{
	mCaptureImpl->captureAudio();

	if( mCaptureImpl->mNumFramesBuffered < buffer->getNumFrames() ) // TODO: mark underrun here first
		return;

	size_t capturedSamplesRead = 0;
	if( mConverter )
		capturedSamplesRead = convertCaptured( buffer );
	else
		capturedSamplesRead = copyCaptured( buffer );

	mCaptureImpl->mNumFramesBuffered -= capturedSamplesRead;
}

size_t LineInWasapi::copyCaptured( Buffer *destBuffer )
{	
	const size_t numChannels = destBuffer->getNumChannels();
	const size_t framesNeeded = destBuffer->getNumFrames();

	if( numChannels == 2 ) {
		if( mCaptureImpl->mRingBuffer->read( mInterleavedBuffer.getData(), framesNeeded * 2 ) )
			dsp::deinterleaveStereoBuffer( &mInterleavedBuffer, destBuffer );
		else
			markUnderrun();
	}
	else if( numChannels == 1 ) {
		if( ! mCaptureImpl->mRingBuffer->read( destBuffer->getData(), framesNeeded ) )
			markUnderrun();
	}
	else
		CI_ASSERT_MSG( 0, "numChannels > 2 not yet supported" );

	return framesNeeded;
}

size_t LineInWasapi::convertCaptured( Buffer *destBuffer )
{
	const size_t numChannels = destBuffer->getNumChannels();
	const size_t framesNeeded = destBuffer->getNumFrames();
	const size_t convertFramesNeeded = size_t( framesNeeded * (float)getDevice()->getSampleRate() / (float)getSampleRate() );

	mConverterReadBuffer.setNumFrames( convertFramesNeeded );

	if( numChannels == 2 ) {
		if( mCaptureImpl->mRingBuffer->read( mInterleavedBuffer.getData(), convertFramesNeeded * 2 ) )
			dsp::deinterleaveStereoBuffer( &mInterleavedBuffer, &mConverterReadBuffer );
		else
			markUnderrun();
	}
	else if( numChannels == 1 ) {
		if( ! mCaptureImpl->mRingBuffer->read( mConverterReadBuffer.getData(), convertFramesNeeded ) )
			markUnderrun();
	}
	else
		CI_ASSERT_MSG( 0, "numChannels > 2 not yet supported" );


	pair<size_t, size_t> count = mConverter->convert( &mConverterReadBuffer, destBuffer );
	return count.first;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - ContextWasapi
// ----------------------------------------------------------------------------------------------------

LineOutRef ContextWasapi::createLineOut( const DeviceRef &device, const Node::Format &format )
{
	return makeNode( new LineOutWasapi( device, format ) );
}

LineInRef ContextWasapi::createLineIn( const DeviceRef &device, const Node::Format &format )
{
	return makeNode( new LineInWasapi( device, format ) );
}

} } } // namespace cinder::audio2::msw

#endif // ( _WIN32_WINNT >= _WIN32_WINNT_VISTA )
