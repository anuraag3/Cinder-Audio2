#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/Utilities.h"


#include "cinder/audio2/Scope.h"
#include "cinder/audio2/SamplePlayer.h"
#include "cinder/audio2/Debug.h"
#include "cinder/audio2/dsp/Dsp.h"

#include "../../common/AudioTestGui.h"
#include "../../../samples/common/AudioDrawUtils.h"
#include "Resources.h"

//#define SOUND_FILE "tone440.wav"
//#define SOUND_FILE "tone440L220R.wav"
//#define SOUND_FILE "Blank__Kytt_-_08_-_RSPN.mp3"
#define SOUND_FILE "cash_satisfied_mind.mp3"

// TODO: make these runtime configurable
#define FFT_SIZE 2048
#define WINDOW_SIZE 1024
#define WINDOW_TYPE audio2::dsp::WindowType::BLACKMAN

using namespace ci;
using namespace ci::app;
using namespace std;


class SpectrumScopeTestApp : public AppNative {
  public:
	void prepareSettings( Settings *settings );
	void fileDrop( FileDropEvent event );
	void setup();
	void update();
	void draw();

	void setupSine();
	void setupSineNoOutput();
	void setupSample();
	void setupUI();
	void processTap( Vec2i pos );
	void processDrag( Vec2i pos );
	void printBinFreq( size_t xPos );


	audio2::BufferPlayerRef			mPlayerNode;
	shared_ptr<audio2::GenSine>		mSine;
	audio2::ScopeSpectralRef		mSpectrumScope;
	audio2::SourceFileRef			mSourceFile;

	vector<TestWidget *>			mWidgets;
	Button							mEnableGraphButton, mPlaybackButton, mLoopButton, mScaleDecibelsButton;
	VSelector						mTestSelector;
	HSlider							mSmoothingFactorSlider, mFreqSlider;
	SpectrumPlot					mSpectrumPlot;
	float							mSpectroMargin;
};


void SpectrumScopeTestApp::prepareSettings( Settings *settings )
{
    settings->setWindowSize( 1200, 500 );
}

void SpectrumScopeTestApp::setup()
{
	mSpectroMargin = 40.0f;

	auto ctx = audio2::Context::master();

	mSpectrumScope = ctx->makeNode( new audio2::ScopeSpectral( audio2::ScopeSpectral::Format().fftSize( FFT_SIZE ).windowSize( WINDOW_SIZE ).windowType( WINDOW_TYPE ) ) );
	mSpectrumScope->setAutoEnabled();

	mSine = ctx->makeNode( new audio2::GenSine() );
	mSine->setFreq( 440.0f );

	mSourceFile = audio2::load( loadResource( RES_CASH_MP3 ) );
	mSourceFile->setOutputFormat( audio2::Context::master()->getSampleRate() );

	auto audioBuffer = mSourceFile->loadBuffer();
	LOG_V( "loaded source buffer, frames: " << audioBuffer->getNumFrames() );

	mPlayerNode = ctx->makeNode( new audio2::BufferPlayer( audioBuffer ) );

	setupSine();

	setupUI();

	ctx->start();
	mEnableGraphButton.setEnabled( true );

	mScaleDecibelsButton.setEnabled( mSpectrumPlot.getScaleDecibels() );

	ctx->printGraph();
}

void SpectrumScopeTestApp::setupSine()
{
	mSine >> mSpectrumScope >> audio2::Context::master()->getOutput();
	if( mPlaybackButton.mEnabled )
		mSine->start();
}

void SpectrumScopeTestApp::setupSineNoOutput()
{
	mSine->connect( mSpectrumScope );
	if( mPlaybackButton.mEnabled )
		mSine->start();
}

void SpectrumScopeTestApp::setupSample()
{
	mPlayerNode >> mSpectrumScope >> audio2::Context::master()->getOutput();
	if( mPlaybackButton.mEnabled )
		mPlayerNode->start();
}

void SpectrumScopeTestApp::setupUI()
{
	Rectf buttonRect( 0.0f, 0.0f, 200.0f, mSpectroMargin - 2.0f );
	float padding = 10.0f;
	mEnableGraphButton.mIsToggle = true;
	mEnableGraphButton.mTitleNormal = "graph off";
	mEnableGraphButton.mTitleEnabled = "graph on";
	mEnableGraphButton.mBounds = buttonRect;
	mWidgets.push_back( &mEnableGraphButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mPlaybackButton.mIsToggle = true;
	mPlaybackButton.mTitleNormal = "play";
	mPlaybackButton.mTitleEnabled = "stop";
	mPlaybackButton.mBounds = buttonRect;
	mWidgets.push_back( &mPlaybackButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mLoopButton.mIsToggle = true;
	mLoopButton.mTitleNormal = "loop off";
	mLoopButton.mTitleEnabled = "loop on";
	mLoopButton.mBounds = buttonRect;
	mWidgets.push_back( &mLoopButton );

	buttonRect += Vec2f( buttonRect.getWidth() + padding, 0.0f );
	mScaleDecibelsButton.mIsToggle = true;
	mScaleDecibelsButton.mTitleNormal = "linear";
	mScaleDecibelsButton.mTitleEnabled = "decibels";
	mScaleDecibelsButton.mBounds = buttonRect;
	mWidgets.push_back( &mScaleDecibelsButton );

	Vec2f sliderSize( 200.0f, 30.0f );
	Rectf selectorRect( getWindowWidth() - sliderSize.x - mSpectroMargin, buttonRect.y2 + padding, getWindowWidth() - mSpectroMargin, buttonRect.y2 + padding + sliderSize.y * 3 );
	mTestSelector.mSegments.push_back( "sine" );
	mTestSelector.mSegments.push_back( "sine (no output)" );
	mTestSelector.mSegments.push_back( "sample" );
	mTestSelector.mBounds = selectorRect;
	mWidgets.push_back( &mTestSelector );

	Rectf sliderRect( selectorRect.x1, selectorRect.y2 + padding, selectorRect.x2, selectorRect.y2 + padding + sliderSize.y );
	mSmoothingFactorSlider.mBounds = sliderRect;
	mSmoothingFactorSlider.mTitle = "Smoothing";
	mSmoothingFactorSlider.mMin = 0.0f;
	mSmoothingFactorSlider.mMax = 1.0f;
	mSmoothingFactorSlider.set( mSpectrumScope->getSmoothingFactor() );
	mWidgets.push_back( &mSmoothingFactorSlider );

	sliderRect += Vec2f( 0.0f, sliderSize.y + padding );
	mFreqSlider.mBounds = sliderRect;
	mFreqSlider.mTitle = "Sine Freq";
	mFreqSlider.mMin = 0.0f;
//	mFreqSlider.mMax = mContext->getSampleRate() / 2.0f;
	mFreqSlider.mMax = 800;
	mFreqSlider.set( mSine->getFreq() );
	mWidgets.push_back( &mFreqSlider );


	getWindow()->getSignalMouseDown().connect( [this] ( MouseEvent &event ) { processTap( event.getPos() ); } );
	getWindow()->getSignalTouchesBegan().connect( [this] ( TouchEvent &event ) { processTap( event.getTouches().front().getPos() ); } );
	getWindow()->getSignalMouseDrag().connect( [this] ( MouseEvent &event ) { processDrag( event.getPos() ); } );
	getWindow()->getSignalTouchesMoved().connect( [this] ( TouchEvent &event ) {
		for( const TouchEvent::Touch &touch : getActiveTouches() )
			processDrag( touch.getPos() );
	} );

	gl::enableAlphaBlending();
}

void SpectrumScopeTestApp::printBinFreq( size_t xPos )
{
	if( xPos < mSpectroMargin || xPos > getWindowWidth() - mSpectroMargin )
		return;

//	freq = bin * samplerate / sizeFft

	size_t numBins = mSpectrumScope->getFftSize() / 2;
	size_t spectroWidth = getWindowWidth() - mSpectroMargin * 2;
	size_t bin = ( numBins * ( xPos - mSpectroMargin ) ) / spectroWidth;
	float freq = bin * audio2::Context::master()->getSampleRate() / float( mSpectrumScope->getFftSize() );

	LOG_V( "bin: " << bin << ", freq: " << freq );
}

// TODO: currently makes sense to enable processor + tap together - consider making these enabled together.
// - possible solution: add a silent flag that is settable by client
void SpectrumScopeTestApp::processTap( Vec2i pos )
{
	auto ctx = audio2::Context::master();
	if( mEnableGraphButton.hitTest( pos ) )
		ctx->setEnabled( ! ctx->isEnabled() );
	else if( mPlaybackButton.hitTest( pos ) ) {
		if( mTestSelector.currentSection() == "sine" || mTestSelector.currentSection() == "sine (no output)" )
			mSine->setEnabled( ! mSine->isEnabled() );
		else
			mPlayerNode->setEnabled( ! mPlayerNode->isEnabled() );
	}
	else if( mLoopButton.hitTest( pos ) )
		mPlayerNode->setLoop( ! mPlayerNode->getLoop() );
	else if( mScaleDecibelsButton.hitTest( pos ) )
		mSpectrumPlot.setScaleDecibels( ! mSpectrumPlot.getScaleDecibels() );
	else
		printBinFreq( pos.x );

	size_t currentIndex = mTestSelector.mCurrentSectionIndex;
	if( mTestSelector.hitTest( pos ) && currentIndex != mTestSelector.mCurrentSectionIndex ) {
		string currentTest = mTestSelector.currentSection();
		LOG_V( "selected: " << currentTest );

		bool enabled = ctx->isEnabled();
		ctx->disconnectAllNodes();

		if( currentTest == "sine" )
			setupSine();
		if( currentTest == "sine (no output)" )
			setupSineNoOutput();
		if( currentTest == "sample" )
			setupSample();

		ctx->setEnabled( enabled );
	}

}

void SpectrumScopeTestApp::processDrag( Vec2i pos )
{
	if( mSmoothingFactorSlider.hitTest( pos ) )
		mSpectrumScope->setSmoothingFactor( mSmoothingFactorSlider.mValueScaled );
	if( mFreqSlider.hitTest( pos ) )
		mSine->setFreq( mFreqSlider.mValueScaled );
}

void SpectrumScopeTestApp::fileDrop( FileDropEvent event )
{
	const fs::path &filePath = event.getFile( 0 );
	LOG_V( "File dropped: " << filePath );

	mSourceFile = audio2::load( loadFile( filePath ) );
	mSourceFile->setOutputFormat( audio2::Context::master()->getSampleRate() );

	mPlayerNode->setBuffer( mSourceFile->loadBuffer() );

	LOG_V( "loaded and set new source buffer, frames: " << mSourceFile->getNumFrames() );
}

void SpectrumScopeTestApp::update()
{
	// update playback button, since the player node may stop itself at the end of a file.
	if( mTestSelector.currentSection() == "sample" && ! mPlayerNode->isEnabled() )
		mPlaybackButton.setEnabled( false );
}

void SpectrumScopeTestApp::draw()
{
	gl::clear();

	// draw magnitude spectrum bins
	auto &mag = mSpectrumScope->getMagSpectrum();
	mSpectrumPlot.setBounds( Rectf( mSpectroMargin, mSpectroMargin, getWindowWidth() - mSpectroMargin, getWindowHeight() - mSpectroMargin ) );
	mSpectrumPlot.draw( mag );
	
	// draw rect around spectrogram boundary
	gl::color( Color::gray( 0.5 ) );
	gl::drawStrokedRect( mSpectrumPlot.getBounds() );

	if( ! mag.empty() ) {
		auto min = min_element( mag.begin(), mag.end() );
		auto max = max_element( mag.begin(), mag.end() );

		string info = string( "min: " ) + toString( *min ) + string( ", max: " ) + toString( *max );
		gl::drawString( info, Vec2f( mSpectroMargin, getWindowHeight() - 30.0f ) );
	}

	drawWidgets( mWidgets );
}

CINDER_APP_NATIVE( SpectrumScopeTestApp, RendererGl )
