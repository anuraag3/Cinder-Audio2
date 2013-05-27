#include "audio2/DeviceInputWasapi.h"
#include "audio2/RingBuffer.h"
#include "audio2/assert.h"
#include "audio2/Debug.h"

using namespace std;

namespace audio2 {

	
// ----------------------------------------------------------------------------------------------------
// MARK: - InputWasapi
// ----------------------------------------------------------------------------------------------------


InputWasapi::InputWasapi( DeviceRef device )
: Input( device )
{
	mTag = "InputWasapi";

	//mDevice = dynamic_pointer_cast<DeviceAudioUnit>( device );
	//CI_ASSERT( mDevice );

	//mFormat.setSampleRate( mDevice->getSampleRate() );
	//mFormat.setNumChannels( 2 );

	//CI_ASSERT( ! mDevice->isInputConnected() );
	//mDevice->setInputConnected();
}

InputWasapi::~InputWasapi()
{
}

void InputWasapi::initialize()
{
	LOG_E << "TODO." << endl;
}

void InputWasapi::uninitialize()
{
	//mDevice->uninitialize();
}

void InputWasapi::start()
{
	//if( ! mDevice->isOutputConnected() ) {
	//	mDevice->start();
	//	LOG_V << "started: " << mDevice->getName() << endl;
	//}
}

void InputWasapi::stop()
{
	//if( ! mDevice->isOutputConnected() ) {
	//	mDevice->stop();
	//	LOG_V << "stopped: " << mDevice->getName() << endl;
	//}
}

DeviceRef InputWasapi::getDevice()
{
	//return std::static_pointer_cast<Device>( mDevice );
	LOG_E << "TODO" << endl;
	return DeviceRef();
}

void InputWasapi::render( BufferT *buffer )
{
	CI_ASSERT( mRingBuffer );

	size_t numFrames = buffer->at( 0 ).size();
	for( size_t c = 0; c < buffer->size(); c++ ) {
		size_t count = mRingBuffer->read( &(*buffer)[c] );
		if( count != numFrames )
			LOG_V << " Warning, unexpected read count: " << count << ", expected: " << numFrames << " (c = " << c << ")" << endl;
	}
}

} // namespace audio2