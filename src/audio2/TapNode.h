#pragma once

#include "audio2/Context.h"

// TODO: getContext().getFramesPerBlock should set the default tap / fft size

namespace audio2 {

class RingBuffer;
class Fft;

typedef std::shared_ptr<class TapNode> TapNodeRef;
typedef std::shared_ptr<class SpectrumTapNode> SpectrumTapNodeRef;

class TapNode : public Node {
public:
	TapNode( size_t numBufferedFrames = 1024 );
	virtual ~TapNode();

	const float* getChannel( size_t ch = 0 );
	const Buffer& getBuffer();

	virtual void initialize() override;
	virtual void process( Buffer *buffer ) override;

private:
	std::vector<std::unique_ptr<RingBuffer> > mRingBuffers; // TODO: make this one continuous buffer so it better matches audio::Buffer
	Buffer mCopiedBuffer;
	size_t mNumBufferedFrames;
};

class SpectrumTapNode : public Node {
public:
	SpectrumTapNode( size_t fftSize = 512 );
	virtual ~SpectrumTapNode();
	
	virtual void initialize() override;
	virtual void process( audio2::Buffer *buffer ) override;

	const std::vector<float>& getMagSpectrum();

	void setWindowingEnabled( bool b = true )	{ mApplyWindow = b; }
	bool isWindowingEnabled() const				{ return mApplyWindow; }

private:

	void copyToInternalBuffer( audio2::Buffer *buffer );
	void applyWindow();

	std::unique_ptr<Fft> mFft;
	std::mutex mMutex;
	audio2::Buffer mBuffer; // TODO: consider storing this in Fft - it has to be the same size as Fft::getSize
	std::vector<float> mMagSpectrum;
	std::atomic<bool> mApplyWindow;
	std::atomic<size_t> mNumFramesCopied;
	size_t mFramesPerBlock;
};

} // namespace audio2