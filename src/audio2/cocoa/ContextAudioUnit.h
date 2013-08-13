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

#pragma once

#include "audio2/Context.h"
#include "audio2/GeneratorNode.h"
#include "audio2/EffectNode.h"
#include "audio2/RingBuffer.h"
#include "audio2/cocoa/CinderCoreAudio.h"

#include <AudioUnit/AudioUnit.h>

namespace audio2 { namespace cocoa {

class DeviceAudioUnit;
class ContextAudioUnit;

class NodeAudioUnit {
  public:
	NodeAudioUnit() : mAudioUnit( nullptr ), mRenderBus( 0 )	{}
	virtual ~NodeAudioUnit();
	virtual ::AudioUnit getAudioUnit() const	{ return mAudioUnit; }
	::AudioUnitScope getRenderBus() const	{ return mRenderBus; }

  protected:

	::AudioUnit			mAudioUnit; // TODO: store these in a unique_ptr with custom deleter - it should only be deleted when the node is
	::AudioUnitScope	mRenderBus;
	Buffer*				mProcessBuffer;

	struct RenderContext {
		Node				*node;
		ContextAudioUnit	*context;
	} mRenderContext;
};

class LineOutAudioUnit : public LineOutNode, public NodeAudioUnit {
  public:
	LineOutAudioUnit( const ContextRef &context, DeviceRef device, const Format &format = Format() );
	virtual ~LineOutAudioUnit() = default;

	std::string virtual getTag() override			{ return "LineOutAudioUnit"; }

	void initialize() override;
	void uninitialize() override;

	void start() override;
	void stop() override;

	::AudioUnit getAudioUnit() const override;
	DeviceRef getDevice() override;

  private:
	static OSStatus renderCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 busNumber, UInt32 numFrames, ::AudioBufferList *bufferList );

	std::shared_ptr<DeviceAudioUnit> mDevice;
};

class LineInAudioUnit : public LineInNode, public NodeAudioUnit {
  public:
	LineInAudioUnit( const ContextRef &context, DeviceRef device, const Format &format = Format() );
	virtual ~LineInAudioUnit();

	std::string virtual getTag() override			{ return "LineInAudioUnit"; }

	void initialize() override;
	void uninitialize() override;

	void start() override;
	void stop() override;

	::AudioUnit getAudioUnit() const override;
	DeviceRef getDevice() override;

	void process( Buffer *buffer ) override;

  private:
	OSStatus renderCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 bus, UInt32 numFrames, ::AudioBufferList *bufferList );
	static OSStatus inputCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 bus, UInt32 numFrames, ::AudioBufferList *bufferList );

	std::shared_ptr<DeviceAudioUnit> mDevice;
	std::unique_ptr<RingBuffer> mRingBuffer;
	AudioBufferListPtr mBufferList;
	bool				mSynchroniousIO;
};

// TODO: when stopped / mEnabled = false; kAudioUnitProperty_BypassEffect should be used
class EffectAudioUnit : public EffectNode, public NodeAudioUnit {
  public:
	EffectAudioUnit( const ContextRef &context, UInt32 subType, const Format &format = Format() );
	virtual ~EffectAudioUnit();

	std::string virtual getTag() override			{ return "EffectAudioUnit"; }

	void initialize() override;
	void uninitialize() override;
	void process( Buffer *buffer ) override;

	void setParameter( ::AudioUnitParameterID param, float val );

  private:
	static OSStatus renderCallback( void *data, ::AudioUnitRenderActionFlags *flags, const ::AudioTimeStamp *timeStamp, UInt32 busNumber, UInt32 numFrames, ::AudioBufferList *bufferList );

	UInt32		mEffectSubType;
	AudioBufferListPtr mBufferList;
};

class MixerAudioUnit : public MixerNode, public NodeAudioUnit {
  public:
	MixerAudioUnit(  const ContextRef &context, const Format &format = Format() );
	virtual ~MixerAudioUnit();

	std::string virtual getTag() override			{ return "MixerAudioUnit"; }

	void initialize() override;
	void uninitialize() override;

	size_t getNumBusses() override;
	void setNumBusses( size_t count ) override;
	void setMaxNumBusses( size_t count );

	bool isBusEnabled( size_t bus ) override;
	void setBusEnabled( size_t bus, bool enabled = true ) override;
	void setBusVolume( size_t bus, float volume ) override;
	float getBusVolume( size_t bus ) override;
	void setBusPan( size_t bus, float pan ) override;
	float getBusPan( size_t bus ) override;

  private:
	void checkBusIsValid( size_t bus );
};

class ContextAudioUnit : public Context {
  public:
	virtual ~ContextAudioUnit();

	virtual ContextRef			createContext() override;
	virtual LineOutNodeRef		createLineOut( DeviceRef device, const Node::Format &format = Node::Format() ) override;
	virtual LineInNodeRef		createLineIn( DeviceRef device, const Node::Format &format = Node::Format() ) override;
	virtual MixerNodeRef		createMixer( const Node::Format &format = Node::Format() ) override;

	void initialize() override;
	void uninitialize() override;

	//! set by the RootNode
	void setCurrentTimeStamp( const ::AudioTimeStamp *timeStamp ) { mCurrentTimeStamp = timeStamp; }
	//! all other NodeAudioUnit's need to pass this correctly formatted timestamp to AudioUnitRender
	const ::AudioTimeStamp* getCurrentTimeStamp() { return mCurrentTimeStamp; }

  private:

	// TODO: consider making these abstract methods in Context
	void initNode( NodeRef node );
	void uninitNode( NodeRef node );

	const ::AudioTimeStamp *mCurrentTimeStamp;
};

} } // namespace audio2::cocoa