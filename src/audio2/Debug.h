#pragma once

#include "cinder/app/App.h"

#define LOG_V ci::app::console() << __FUNCTION__ << " | "
#define LOG_E LOG_V << __LINE__ << " | " << " *** ERROR *** : "

#include <boost/assert.hpp>

#define CI_ASSERT(expr) BOOST_ASSERT(expr)

