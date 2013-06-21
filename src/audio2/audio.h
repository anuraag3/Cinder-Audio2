#pragma once

#include "audio2/Context.h"

#include "cinder/Cinder.h"
#include "cinder/Exception.h"
//#include <string>

namespace audio2 {

void printGraph( ContextRef graph );
void printDevices();

class AudioExc : public ci::Exception {
public:
	AudioExc( const std::string &description ) : mDescription( description )	{}
	virtual const char* what() const throw()	{ return mDescription.c_str(); }
protected:
	std::string mDescription;
};

class AudioDeviceExc : public AudioExc {
public:
	AudioDeviceExc( const std::string &description ) : AudioExc( description )	{}
};

class AudioContextExc : public AudioExc {
public:
	AudioContextExc( const std::string &description ) : AudioExc( description )	{}
};

class AudioFormatExc : public AudioExc {
public:
	AudioFormatExc( const std::string &description ) : AudioExc( description )	{}
};

class AudioParamExc : public AudioExc {
public:
	AudioParamExc( const std::string &description ) : AudioExc( description )	{}
};

class AudioFileExc : public AudioExc {
public:
	AudioFileExc( const std::string &description ) : AudioExc( description )	{}
};

} // namespace audio2