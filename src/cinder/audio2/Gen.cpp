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

#include "cinder/audio2/Gen.h"
#include "cinder/audio2/Context.h"
#include "cinder/audio2/dsp/Dsp.h"
#include "cinder/audio2/Debug.h"
#include "cinder/Rand.h"

using namespace std;

namespace cinder { namespace audio2 {

// ----------------------------------------------------------------------------------------------------
// MARK: - Gen
// ----------------------------------------------------------------------------------------------------

Gen::Gen( const Format &format )
	: NodeInput( format ), mFreq( this ), mPhase( 0 )
{
	mChannelMode = ChannelMode::SPECIFIED;
	setNumChannels( 1 );
}

void Gen::initialize()
{
	mSampleRate = (float)getContext()->getSampleRate();
}

// ----------------------------------------------------------------------------------------------------
// MARK: - GenNoise
// ----------------------------------------------------------------------------------------------------

void GenNoise::process( Buffer *buffer )
{
	float *data = buffer->getData();
	size_t count = buffer->getSize();

	for( size_t i = 0; i < count; i++ )
		data[i] = ci::randFloat( -1.0f, 1.0f );
}

// ----------------------------------------------------------------------------------------------------
// MARK: - GenSine
// ----------------------------------------------------------------------------------------------------

void GenSine::process( Buffer *buffer )
{
	float *data = buffer->getData();
	const size_t count = buffer->getSize();
	const float phaseMul = float( 2 * M_PI / (double)mSampleRate );
	float phase = mPhase;

	if( mFreq.eval() ) {
		float *freqValues = mFreq.getValueArray();
		for( size_t i = 0; i < count; i++ ) {
			data[i] = math<float>::sin( phase );
			phase = fmodf( phase + freqValues[i] * phaseMul, float( M_PI * 2 ) );
		}
	}
	else {
		const float phaseIncr = mFreq.getValue() * phaseMul;
		for( size_t i = 0; i < count; i++ ) {
			data[i] = math<float>::sin( phase );
			phase = fmodf( phase + phaseIncr, float( M_PI * 2 ) );
		}
	}

	mPhase = phase;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - GenPhasor
// ----------------------------------------------------------------------------------------------------

void GenPhasor::process( Buffer *buffer )
{
	float *data = buffer->getData();
	const size_t count = buffer->getSize();
	const float phaseMul = 1.0f / mSampleRate;
	float phase = mPhase;

	if( mFreq.eval() ) {
		float *freqValues = mFreq.getValueArray();
		for( size_t i = 0; i < count; i++ ) {
			data[i] = phase;
			phase = fmodf( phase + freqValues[i] * phaseMul, 1 );
		}
	}
	else {
		const float phaseIncr = mFreq.getValue() * phaseMul;
		for( size_t i = 0; i < count; i++ ) {
			data[i] = phase;
			phase = fmodf( phase + phaseIncr, 1 );
		}
	}

	mPhase = phase;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - GenTriangle
// ----------------------------------------------------------------------------------------------------

namespace {

inline float calcTriangleSignal( float phase, float upSlope, float downSlope )
{
	// if up slope = down slope = 1, signal ranges from 0 to 0.5. so normalize this from -1 to 1
	float signal = std::min( phase * upSlope, ( 1 - phase ) * downSlope );
	return signal * 4 - 1;
}

} // anonymous namespace

void GenTriangle::process( Buffer *buffer )
{
	const float phaseMul = 1.0f / mSampleRate;
	float *data = buffer->getData();
	size_t count = buffer->getSize();
	float phase = mPhase;

	if( mFreq.eval() ) {
		float *freqValues = mFreq.getValueArray();
		for( size_t i = 0; i < count; i++ )	{
			data[i] = calcTriangleSignal( phase, mUpSlope, mDownSlope );
			phase = fmodf( phase + freqValues[i] * phaseMul, 1.0f );
		}

	}
	else {
		const float phaseIncr = mFreq.getValue() * phaseMul;
		for( size_t i = 0; i < count; i++ )	{
			data[i] = calcTriangleSignal( phase, mUpSlope, mDownSlope );
			phase = fmodf( phase + phaseIncr, 1 );
		}
	}

	mPhase = phase;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - GenOscillator
// ----------------------------------------------------------------------------------------------------

GenOscillator::GenOscillator( const Format &format )
	: Gen( format ), mWaveformType( format.getWaveform() )
{
}

void GenOscillator::initialize()
{
	Gen::initialize();

	if( ! mWaveTable ) {
		mWaveTable.reset( new dsp::WaveTable( mSampleRate ) );
		mWaveTable->fill( mWaveformType );
	}
	else if( mSampleRate != mWaveTable->getSampleRate() ) {
		mWaveTable->resize( mSampleRate );
		mWaveTable->fill( mWaveformType );
	}
}

void GenOscillator::setWaveform( WaveformType type )
{
	if( mWaveformType == type )
		return;

	// TODO: to prevent the entire graph from blocking, use our own lock and tryLock / fail when blocked in process()
	lock_guard<mutex> lock( getContext()->getMutex() );

	mWaveformType = type;
	mWaveTable->fill( type );
}

namespace {

#if 0

// truncate, phase range: 0-1
inline float tableLookup( const float *table, size_t size, float phase )
{
	return table[ size_t( phase * size ) ];
}

#else

// linear interpolation, phase range: 0-1
// TODO (optimization): store phase in range 0-size
inline float tableLookup( const float *table, size_t size, float phase )
{
	float lookup = phase * size;
	size_t index1 = (size_t)lookup;
//	size_t index2 = ( index1 + 1 ) % size;
	size_t index2 = ( index1 + 1 ) & ( size - 1 );
	float val1 = table[index1];
	float val2 = table[index2];
	float frac = lookup - (float)index1;

	return val2 + frac * ( val2 - val1 );
}

#endif

} // anonymous namespace

#if 1

// no table interp
void GenOscillator::process( Buffer *buffer )
{
	const size_t count = buffer->getSize();
	const float tableSize = mWaveTable->getTableSize();
	const float samplePeriod = 1.0f / mSampleRate;
	float *data = buffer->getData();
	float phase = mPhase;

	if( mFreq.eval() ) {
		float *freqValues = mFreq.getValueArray();
		for( size_t i = 0; i < count; i++ ) {

			float f0 = freqValues[i];
			const float *table = mWaveTable->getBandLimitedTable( f0 );

			data[i] = tableLookup( table, tableSize, phase );
			phase = fmodf( phase + freqValues[i] * samplePeriod, 1 );
		}
	}
	else {

		float f0 = mFreq.getValue();
		const float *table = mWaveTable->getBandLimitedTable( f0 );

		const float phaseIncr = f0 * samplePeriod;

		for( size_t i = 0; i < count; i++ ) {
			data[i] = tableLookup( table, tableSize, phase );
			phase = fmodf( phase + phaseIncr, 1 );
		}
	}

	mPhase = phase;
}

#else

// linear table interp
void GenOscillator::process( Buffer *buffer )
{
	const size_t count = buffer->getSize();
	const float tableSize = mWaveTable->getTableSize();
	const float samplePeriod = 1.0f / mSampleRate;
	float *data = buffer->getData();
	float phase = mPhase;

	if( mFreq.eval() ) {
		float *freqValues = mFreq.getValueArray();
		float *table1, *table2;
		float factor;

		for( size_t i = 0; i < count; i++ ) {

			float f0 = freqValues[i];
			mWaveTable->getBandLimitedTables( f0, &table1, &table2, &factor );

			float a = tableLookup( table1, tableSize, phase );
			float b = tableLookup( table2, tableSize, phase );
			data[i] = lerp( a, b, factor );
			phase = fmodf( phase + freqValues[i] * samplePeriod, 1 );
		}
	}
	else {
		float f0 = mFreq.getValue();

		float *table1, *table2;
		float factor;
		mWaveTable->getBandLimitedTables( f0, &table1, &table2, &factor );

		const float phaseIncr = f0 * samplePeriod;

		for( size_t i = 0; i < count; i++ ) {
			float a = tableLookup( table1, tableSize, phase );
			float b = tableLookup( table2, tableSize, phase );
			data[i] = lerp( a, b, factor );
			phase = fmodf( phase + phaseIncr, 1 );
		}
	}
	
	mPhase = phase;
}

#endif

} } // namespace cinder::audio2