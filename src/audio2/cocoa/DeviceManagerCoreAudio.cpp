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

#include "audio2/cocoa/DeviceManagerCoreAudio.h"
#include "audio2/audio.h"

#include "audio2/Debug.h"

#include "cinder/cocoa/CinderCocoa.h"

using namespace std;
using namespace ci;

namespace cinder { namespace audio2 { namespace cocoa {

// ----------------------------------------------------------------------------------------------------
// MARK: - Private AudioObject Helpers
// ----------------------------------------------------------------------------------------------------

namespace {

AudioObjectPropertyAddress getAudioObjectPropertyAddress( ::AudioObjectPropertySelector propertySelector, ::AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal )
{
	::AudioObjectPropertyAddress result;
	result.mSelector = propertySelector;
	result.mScope = scope;
	result.mElement = kAudioObjectPropertyElementMaster;
	return result;
}

UInt32 getAudioObjectPropertyDataSize( ::AudioObjectID objectId, ::AudioObjectPropertyAddress propertyAddress, UInt32 qualifierDataSize = 0, const void *qualifierData = NULL )
{
	UInt32 result = 0;
	OSStatus status = ::AudioObjectGetPropertyDataSize( objectId, &propertyAddress, qualifierDataSize, qualifierData, &result );
	CI_ASSERT( status == noErr );

	return result;
}

void getAudioObjectPropertyData( ::AudioObjectID objectId, ::AudioObjectPropertyAddress& propertyAddress, UInt32 dataSize, void *data, UInt32 qualifierDataSize = 0, const void *qualifierData = NULL )
{
	OSStatus status = ::AudioObjectGetPropertyData( objectId, &propertyAddress, qualifierDataSize, qualifierData, &dataSize, data );
	CI_ASSERT( status == noErr );
}

string getAudioObjectPropertyString( ::AudioObjectID objectId, ::AudioObjectPropertySelector propertySelector )
{
	::AudioObjectPropertyAddress property = getAudioObjectPropertyAddress( propertySelector );
	if( !::AudioObjectHasProperty( objectId, &property ) )
		return string();

	CFStringRef resultCF;
	UInt32 cfStringSize = sizeof( CFStringRef );

	OSStatus status = ::AudioObjectGetPropertyData( objectId, &property, 0, NULL, &cfStringSize, &resultCF );
	CI_ASSERT( status == noErr );

	string result = ci::cocoa::convertCfString( resultCF );
	CFRelease( resultCF );
	return result;
}

size_t getAudioObjectNumChannels( ::AudioObjectID objectId, bool isInput )
{
	::AudioObjectPropertyAddress streamConfigProperty = getAudioObjectPropertyAddress( kAudioDevicePropertyStreamConfiguration, isInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput );
	UInt32 streamConfigPropertySize = getAudioObjectPropertyDataSize( objectId, streamConfigProperty );
	shared_ptr<::AudioBufferList> bufferList( (::AudioBufferList *)calloc( 1, streamConfigPropertySize ), free );

	getAudioObjectPropertyData( objectId, streamConfigProperty, streamConfigPropertySize, bufferList.get() );

	size_t numChannels = 0;
	for( int i = 0; i < bufferList->mNumberBuffers; i++ ) {
		numChannels += bufferList->mBuffers[i].mNumberChannels;
	}
	return numChannels;
}

template<typename PropT>
void setAudioObjectProperty( ::AudioObjectID objectId, ::AudioObjectPropertyAddress& propertyAddress, const PropT &data, UInt32 qualifierDataSize = 0, const void *qualifierData = NULL )
{
	UInt32 dataSize = sizeof( PropT );
	OSStatus status = ::AudioObjectSetPropertyData( objectId, &propertyAddress, qualifierDataSize, qualifierData, dataSize, &data );
	CI_ASSERT( status == noErr );
}

template<typename PropT>
PropT getAudioObjectProperty( ::AudioObjectID objectId, ::AudioObjectPropertyAddress& propertyAddress, UInt32 qualifierDataSize = 0, const void *qualifierData = NULL )
{
	PropT result;
	UInt32 resultSize = sizeof( result );
	
	OSStatus status = ::AudioObjectGetPropertyData( objectId, &propertyAddress, qualifierDataSize, qualifierData, &resultSize, &result );
	CI_ASSERT( status == noErr );

	return result;
}

template<typename PropT>
vector<PropT> getAudioObjectPropertyVector( ::AudioObjectID objectId, ::AudioObjectPropertySelector propertySelector, ::AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal )
{
	vector<PropT> result;
	::AudioObjectPropertyAddress propAddress = getAudioObjectPropertyAddress( propertySelector, scope );
	UInt32 propSize = getAudioObjectPropertyDataSize( objectId, propAddress );
	result.resize( propSize / sizeof( PropT ) );

	getAudioObjectPropertyData( objectId, propAddress, propSize, result.data() );
	return result;
}

} // anonymous namespace

// ----------------------------------------------------------------------------------------------------
// MARK: - DeviceManagerCoreAudio
// ----------------------------------------------------------------------------------------------------

DeviceRef DeviceManagerCoreAudio::getDefaultOutput()
{
	::AudioObjectPropertyAddress propertyAddress = getAudioObjectPropertyAddress( kAudioHardwarePropertyDefaultOutputDevice );
	auto defaultOutputId = getAudioObjectProperty<::AudioDeviceID>( kAudioObjectSystemObject, propertyAddress );

	return findDeviceByKey( DeviceManagerCoreAudio::keyForDeviceId( defaultOutputId ) );
}

DeviceRef DeviceManagerCoreAudio::getDefaultInput()
{
	::AudioObjectPropertyAddress propertyAddress = getAudioObjectPropertyAddress( kAudioHardwarePropertyDefaultInputDevice );
	auto defaultInputId = getAudioObjectProperty<::AudioDeviceID>( kAudioObjectSystemObject, propertyAddress );

	return findDeviceByKey( DeviceManagerCoreAudio::keyForDeviceId( defaultInputId ) );
}

void DeviceManagerCoreAudio::setCurrentDevice( const DeviceRef &device, ::AudioComponentInstance componentInstance )
{
	::AudioDeviceID deviceId = mDeviceIds.at( device );

	OSStatus status = ::AudioUnitSetProperty( componentInstance, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &deviceId, sizeof( deviceId ) );
	CI_ASSERT( status == noErr );

	registerPropertyListeners( device, deviceId, componentInstance );
}

std::string DeviceManagerCoreAudio::getName( const string &key )
{
	::AudioDeviceID deviceId = getDeviceId( key );
	return getAudioObjectPropertyString( deviceId, kAudioObjectPropertyName );
}

size_t DeviceManagerCoreAudio::getNumInputChannels( const string &key )
{
	::AudioDeviceID deviceId = getDeviceId( key );
	return getAudioObjectNumChannels( deviceId, true );
}

size_t DeviceManagerCoreAudio::getNumOutputChannels( const string &key )
{
	::AudioDeviceID deviceId = getDeviceId( key );
	return getAudioObjectNumChannels( deviceId, false );
}

size_t DeviceManagerCoreAudio::getSampleRate( const string &key )
{
	//	vector<::AudioValueRange> nsr = getAudioObjectPropertyVector<::AudioValueRange>( deviceId, kAudioDevicePropertyAvailableNominalSampleRates );

	::AudioDeviceID deviceId = getDeviceId( key );
	::AudioObjectPropertyAddress propertyAddress = getAudioObjectPropertyAddress( kAudioDevicePropertyNominalSampleRate );
	auto result = getAudioObjectProperty<Float64>( deviceId, propertyAddress );

	return static_cast<size_t>( result );
}

void DeviceManagerCoreAudio::setSampleRate( const std::string &key, size_t sampleRate )
{
	::AudioDeviceID deviceId = getDeviceId( key );
	::AudioObjectPropertyAddress property = getAudioObjectPropertyAddress( kAudioDevicePropertyNominalSampleRate );

	size_t currentSr = getSampleRate( key );
	LOG_V << "current samplerate: " << currentSr << endl;

	LOG_V << "... setting to: " << sampleRate << endl;

	Float64 data = static_cast<Float64>( sampleRate );
	setAudioObjectProperty( deviceId, property, data );

	size_t resultSr = getSampleRate( key );
	LOG_V << "... result: " << resultSr << endl;
}

size_t DeviceManagerCoreAudio::getFramesPerBlock( const string &key )
{
	::AudioDeviceID deviceId = getDeviceId( key );
	::AudioObjectPropertyAddress propertyAddress = getAudioObjectPropertyAddress( kAudioDevicePropertyBufferFrameSize );
	auto result = getAudioObjectProperty<UInt32>( deviceId, propertyAddress );

	return static_cast<size_t>( result );
}

void DeviceManagerCoreAudio::setFramesPerBlock( const std::string &key, size_t framesPerBlock )
{
	::AudioDeviceID deviceId = getDeviceId( key );
	::AudioObjectPropertyAddress property = getAudioObjectPropertyAddress( kAudioDevicePropertyBufferFrameSize );

	UInt32 data = static_cast<UInt32>( framesPerBlock );
	setAudioObjectProperty( deviceId, property, data );
}

// ----------------------------------------------------------------------------------------------------
// MARK: - Private
// ----------------------------------------------------------------------------------------------------

// note: device doesn't need to be copied because DeviceManagerCoreAudio owns it.
// TODO: if device is considered 'default', register for kAudioHardwarePropertyDefaultOutputDevice and update when required
void DeviceManagerCoreAudio::registerPropertyListeners( const DeviceRef &device, ::AudioDeviceID deviceId, ::AudioComponentInstance componentInstance )
{
	AudioObjectPropertyListenerBlock listenerBlock = ^( UInt32 inNumberAddresses, const AudioObjectPropertyAddress inAddresses[] ) {

		LOG_V << "# properties changed: " << inNumberAddresses << endl;
		bool paramsUpdated = false;

		for( UInt32 i = 0; i < inNumberAddresses; i++ ) {

			AudioObjectPropertyAddress propertyAddress = inAddresses[i];
			if( propertyAddress.mSelector == kAudioDevicePropertyDataSource ) {
				UInt32 dataSource = getAudioObjectProperty<UInt32>( deviceId, propertyAddress );

				::AudioObjectPropertyAddress dataSourceNameAddress = getAudioObjectPropertyAddress( kAudioDevicePropertyDataSourceNameForIDCFString, kAudioDevicePropertyScopeOutput );;
				CFStringRef dataSourceNameCF;
				::AudioValueTranslation translation = { &dataSource, sizeof( UInt32 ), &dataSourceNameCF, sizeof( CFStringRef ) };
				getAudioObjectPropertyData( deviceId, dataSourceNameAddress, sizeof( AudioValueTranslation ), &translation );

				string dataSourceName = ci::cocoa::convertCfString( dataSourceNameCF );
				CFRelease( dataSourceNameCF );

				LOG_V << "device data source changed to: " << dataSourceName << endl;
			}
			else if( propertyAddress.mSelector == kAudioDevicePropertyNominalSampleRate ) {
				paramsUpdated = true;
				auto result = getAudioObjectProperty<Float64>( deviceId, propertyAddress );
				LOG_V << "device samplerate changed to: " << (int)result << endl;
			}
			else if( propertyAddress.mSelector == kAudioDevicePropertyBufferFrameSize ) {
				paramsUpdated = true;

				auto result = getAudioObjectProperty<UInt32>( deviceId, propertyAddress );
				LOG_V << "device samplerate changed to: " << (int)result << endl;
			}
		}

		if( paramsUpdated )
			emitParamsDidChange( device );
    };

	// data source (ex. internal speakers, headphones)
	::AudioObjectPropertyAddress dataSourceAddress = getAudioObjectPropertyAddress( kAudioDevicePropertyDataSource, kAudioDevicePropertyScopeOutput );
	::AudioObjectAddPropertyListenerBlock( deviceId, &dataSourceAddress, dispatch_get_current_queue(), listenerBlock );

	// device samplerate
	::AudioObjectPropertyAddress samplerateAddress = getAudioObjectPropertyAddress( kAudioDevicePropertyNominalSampleRate );
	::AudioObjectAddPropertyListenerBlock( deviceId, &samplerateAddress, dispatch_get_current_queue(), listenerBlock );

	// frames per block
	::AudioObjectPropertyAddress frameSizeAddress = getAudioObjectPropertyAddress( kAudioDevicePropertyBufferFrameSize );
	::AudioObjectAddPropertyListenerBlock( deviceId, &frameSizeAddress, dispatch_get_current_queue(), listenerBlock );
}

::AudioDeviceID DeviceManagerCoreAudio::getDeviceId( const std::string &key )
{
	CI_ASSERT( ! mDeviceIds.empty() );

	for( const auto& devicePair : mDeviceIds ) {
		if( devicePair.first->getKey() == key )
			return devicePair.second;
	}

	CI_ASSERT( 0 && "unreachable" );
}

const std::vector<DeviceRef>& DeviceManagerCoreAudio::getDevices()
{
	if( mDevices.empty() ) {
		auto deviceIds = getAudioObjectPropertyVector<::AudioObjectID>( kAudioObjectSystemObject, kAudioHardwarePropertyDevices );
		for ( ::AudioDeviceID &deviceId : deviceIds ) {
			string key = keyForDeviceId( deviceId );
			auto device = addDevice( key );
			mDeviceIds.insert( { device, deviceId } );
		}
	}
	return mDevices;
}

// note: we cannot just rely on 'model UID', when it is there (which it isn't always), becasue it can be the same
// for two different 'devices', such as system input and output
// - current solution: key = 'NAME-[UID | MANUFACTURER]'
std::string DeviceManagerCoreAudio::keyForDeviceId( ::AudioDeviceID deviceId )
{
	string name = getAudioObjectPropertyString( deviceId, kAudioObjectPropertyName );
	string key = getAudioObjectPropertyString( deviceId, kAudioDevicePropertyModelUID );
	if( key.empty() )
		key = getAudioObjectPropertyString( deviceId, kAudioObjectPropertyManufacturer );

	return name + " - " + key;
}

} } } // namespace cinder::audio2::cocoa